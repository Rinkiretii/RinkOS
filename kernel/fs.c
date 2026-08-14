#include <stdint.h>
#include <stddef.h>
#include "scr/fs.h"
#include "scr/disk.h"

extern void kprint(const char *);
extern void kprint_hex32(uint32_t);

/* Root directory, cached in RAM and flushed to disk on every change -
 * same pattern the old flat-table driver used, just FAT-shaped now. */
static fs_dirent_t root[FS_ROOT_ENTRIES];

/* ---------- tiny string helpers (no libc available) ---------- */

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

/* Splits a user-typed "name.ext" (any case, any length) into the
 * space-padded, uppercased 8.3 fields used on disk. */
static void split_name(const char *in, uint8_t name8[FS_NAME_FIELD_LEN], uint8_t ext3[FS_EXT_FIELD_LEN])
{
    int i;
    for (i = 0; i < FS_NAME_FIELD_LEN; i++) name8[i] = ' ';
    for (i = 0; i < FS_EXT_FIELD_LEN; i++) ext3[i] = ' ';

    int pos = 0;
    int n = 0;
    while (in[pos] && in[pos] != '.' && n < FS_NAME_FIELD_LEN) {
        name8[n++] = (uint8_t)to_upper(in[pos]);
        pos++;
    }
    /* skip rest of an over-long name up to the dot */
    while (in[pos] && in[pos] != '.') pos++;

    if (in[pos] == '.') {
        pos++;
        int e = 0;
        while (in[pos] && e < FS_EXT_FIELD_LEN) {
            ext3[e++] = (uint8_t)to_upper(in[pos]);
            pos++;
        }
    }
}

static int fields_eq(const uint8_t *a, const uint8_t *b, int len)
{
    for (int i = 0; i < len; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Rebuilds a display string "name.ext" (trimmed of padding) from a
 * directory entry, into a buffer of at least FS_NAME_LEN bytes. */
static void dirent_to_display(fs_dirent_t *d, char *out)
{
    int o = 0;
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) {
        if (d->name[i] == ' ') break;
        out[o++] = (char)d->name[i];
    }
    if (d->ext[0] != ' ') {
        out[o++] = '.';
        for (int i = 0; i < FS_EXT_FIELD_LEN; i++) {
            if (d->ext[i] == ' ') break;
            out[o++] = (char)d->ext[i];
        }
    }
    out[o] = '\0';
}

/* ---------- root directory cache ---------- */

static void root_load(void)
{
    uint8_t *buf = (uint8_t *)root;
    for (uint32_t s = 0; s < FS_ROOT_DIR_SECTORS; s++) {
        disk_read_sector(FS_ROOT_LBA + s, buf + s * FS_BYTES_PER_SECTOR);
    }
}

static void root_save(void)
{
    uint8_t *buf = (uint8_t *)root;
    for (uint32_t s = 0; s < FS_ROOT_DIR_SECTORS; s++) {
        disk_write_sector(FS_ROOT_LBA + s, buf + s * FS_BYTES_PER_SECTOR);
    }
}

static int find_slot(const uint8_t name8[FS_NAME_FIELD_LEN], const uint8_t ext3[FS_EXT_FIELD_LEN])
{
    for (int i = 0; i < FS_ROOT_ENTRIES; i++) {
        uint8_t first = root[i].name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) continue;
        if (fields_eq(root[i].name, name8, FS_NAME_FIELD_LEN) &&
            fields_eq(root[i].ext, ext3, FS_EXT_FIELD_LEN)) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < FS_ROOT_ENTRIES; i++) {
        uint8_t first = root[i].name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) return i;
    }
    return -1;
}

/* ---------- FAT table access ---------- */
/* FAT copy #2 always mirrors FAT copy #1 exactly - we only ever read
 * FAT1 and write both, so they can never drift apart. */

static uint16_t fat_read(uint16_t cluster)
{
    uint32_t byte_off = (uint32_t)cluster * 2;
    uint32_t sector = FS_FAT1_LBA + byte_off / FS_BYTES_PER_SECTOR;
    uint32_t off = byte_off % FS_BYTES_PER_SECTOR;

    uint8_t buf[FS_BYTES_PER_SECTOR];
    disk_read_sector(sector, buf);
    return (uint16_t)(buf[off] | (buf[off + 1] << 8));
}

static void fat_write(uint16_t cluster, uint16_t value)
{
    uint32_t byte_off = (uint32_t)cluster * 2;
    uint32_t sector_index = byte_off / FS_BYTES_PER_SECTOR;
    uint32_t off = byte_off % FS_BYTES_PER_SECTOR;

    uint8_t buf[FS_BYTES_PER_SECTOR];
    uint32_t sector1 = FS_FAT1_LBA + sector_index;
    disk_read_sector(sector1, buf);
    buf[off] = (uint8_t)(value & 0xFF);
    buf[off + 1] = (uint8_t)((value >> 8) & 0xFF);
    disk_write_sector(sector1, buf);

    /* FAT2 mirrors FAT1 - same sector layout, same bytes */
    disk_write_sector(FS_FAT2_LBA + sector_index, buf);
}

static uint16_t fat_alloc_cluster(void)
{
    for (uint16_t c = FS_FIRST_DATA_CLUSTER; c < FS_FIRST_DATA_CLUSTER + FS_TOTAL_DATA_CLUSTERS; c++) {
        if (fat_read(c) == FS_CLUSTER_FREE) {
            fat_write(c, FS_CLUSTER_EOC);
            return c;
        }
    }
    return 0; /* disk full */
}

static void fat_free_chain(uint16_t first)
{
    uint16_t c = first;
    while (c >= FS_FIRST_DATA_CLUSTER && c < FS_CLUSTER_EOC_MIN) {
        uint16_t next = fat_read(c);
        fat_write(c, FS_CLUSTER_FREE);
        c = next;
    }
}

static uint32_t cluster_to_lba(uint16_t cluster)
{
    return FS_DATA_LBA + ((uint32_t)cluster - FS_FIRST_DATA_CLUSTER) * FS_SECTORS_PER_CLUSTER;
}

/* Walks `index` links forward from `first`. Returns 0 if the chain is
 * shorter than expected (shouldn't happen unless the FS is corrupt). */
static uint16_t cluster_at_index(uint16_t first, uint32_t index)
{
    uint16_t c = first;
    for (uint32_t i = 0; i < index; i++) {
        if (c < FS_FIRST_DATA_CLUSTER || c >= FS_CLUSTER_EOC_MIN) return 0;
        c = fat_read(c);
    }
    return c;
}

/* ---------- public API ---------- */

void fs_init(void)
{
    uint8_t buf[FS_BYTES_PER_SECTOR];
    disk_read_sector(FS_BOOT_LBA, buf);

    /* fs_type-style marker at offset 54, same slot a real BPB uses */
    static const char marker[8] = "RINKFS16";
    int formatted = 1;
    for (int i = 0; i < 8; i++) {
        if (buf[54 + i] != (uint8_t)marker[i]) { formatted = 0; break; }
    }

    if (!formatted) {
        for (int i = 0; i < FS_BYTES_PER_SECTOR; i++) buf[i] = 0;

        /* --- boot sector / BPB, laid out like a real FAT16 BPB --- */
        buf[0] = 0xEB; buf[1] = 0x3C; buf[2] = 0x90;             /* jmp short + nop */
        const char oem[8] = "RINKOS  ";
        for (int i = 0; i < 8; i++) buf[3 + i] = (uint8_t)oem[i];

        *(uint16_t *)(buf + 11) = FS_BYTES_PER_SECTOR;
        buf[13] = FS_SECTORS_PER_CLUSTER;
        *(uint16_t *)(buf + 14) = (FS_FAT1_LBA - FS_BOOT_LBA);   /* reserved sectors */
        buf[16] = FS_NUM_FATS;
        *(uint16_t *)(buf + 17) = FS_ROOT_ENTRIES;
        *(uint16_t *)(buf + 19) = (FS_IMAGE_TOTAL_SECTORS - FS_VOL_START_LBA); /* total sectors, this volume */
        buf[21] = 0xF8;                                          /* media descriptor: fixed disk */
        *(uint16_t *)(buf + 22) = FS_FAT_SIZE_SECTORS;
        *(uint16_t *)(buf + 24) = 0;                              /* sectors per track (unused by us) */
        *(uint16_t *)(buf + 26) = 0;                              /* heads (unused by us) */
        *(uint32_t *)(buf + 28) = FS_VOL_START_LBA;               /* hidden sectors before this volume */
        *(uint32_t *)(buf + 32) = 0;
        buf[36] = 0x80;
        buf[37] = 0;
        buf[38] = 0x29;                                           /* extended boot signature */
        *(uint32_t *)(buf + 39) = 0x52494E4B;                     /* volume id: 'RINK' */
        const char label[11] = "RINKOS FS  ";
        for (int i = 0; i < 11; i++) buf[43 + i] = (uint8_t)label[i];
        for (int i = 0; i < 8; i++) buf[54 + i] = (uint8_t)marker[i];

        disk_write_sector(FS_BOOT_LBA, buf);

        /* zero out both FAT copies */
        for (int i = 0; i < FS_BYTES_PER_SECTOR; i++) buf[i] = 0;
        for (uint32_t s = 0; s < FS_FAT_SIZE_SECTORS; s++) {
            disk_write_sector(FS_FAT1_LBA + s, buf);
            disk_write_sector(FS_FAT2_LBA + s, buf);
        }
        fat_write(0, FS_CLUSTER_BAD);   /* conventional: mirrors media descriptor */
        fat_write(1, FS_CLUSTER_EOC);   /* conventional: reserved */

        /* zero the root directory, in memory and on disk */
        for (int i = 0; i < FS_ROOT_ENTRIES; i++) {
            root[i].name[0] = FS_DIRENT_FREE;
            root[i].first_cluster = 0;
            root[i].file_size = 0;
        }
        root_save();

//        kprint("Filesystem formatted (FAT16).\n");
    } else {
        root_load();
//        kprint("Filesystem loaded.\n");
    }
}

int fs_create(const char *name)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    if (find_slot(n, e) >= 0) return -1; /* already exists */

    int slot = find_free_slot();
    if (slot < 0) return -1; /* root directory full */

    fs_dirent_t *d = &root[slot];
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) d->name[i] = n[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) d->ext[i] = e[i];
    d->attr = FS_ATTR_NORMAL;
    for (int i = 0; i < 10; i++) d->reserved[i] = 0;
    d->time = 0;
    d->date = 0;
    d->first_cluster = 0;
    d->file_size = 0;

    root_save();
    return slot;
}

int fs_write(const char *name, const uint8_t *data, uint32_t size)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    int slot = find_slot(n, e);
    if (slot < 0) {
        slot = fs_create(name);
        if (slot < 0) return -1;
    }
    fs_dirent_t *d = &root[slot];

    if (d->first_cluster != 0) {
        fat_free_chain(d->first_cluster);
        d->first_cluster = 0;
    }
    d->file_size = 0;

    if (size == 0) {
        root_save();
        return 0;
    }

    uint32_t clusters_needed = (size + FS_BYTES_PER_SECTOR - 1) / FS_BYTES_PER_SECTOR;
    uint16_t first = 0, prev = 0;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];
    uint32_t written = 0;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t c = fat_alloc_cluster();
        if (!c) {
            if (first) fat_free_chain(first);
            d->first_cluster = 0;
            d->file_size = 0;
            root_save();
            return -1; /* disk full */
        }
        if (!first) first = c; else fat_write(prev, c);
        prev = c;

        for (int b = 0; b < FS_BYTES_PER_SECTOR; b++) sector_buf[b] = 0;
        uint32_t remaining = size - written;
        uint32_t chunk = remaining < FS_BYTES_PER_SECTOR ? remaining : FS_BYTES_PER_SECTOR;
        for (uint32_t b = 0; b < chunk; b++) sector_buf[b] = data[written + b];

        disk_write_sector(cluster_to_lba(c), sector_buf);
        written += chunk;
    }

    d->first_cluster = first;
    d->file_size = size;
    root_save();
    return 0;
}

int fs_read(const char *name, uint8_t *buffer, uint32_t max_size, uint32_t *out_size)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    int slot = find_slot(n, e);
    if (slot < 0) return -1;
    fs_dirent_t *d = &root[slot];

    uint32_t size = d->file_size;
    if (size > max_size) size = max_size;

    uint32_t sectors_needed = (size + FS_BYTES_PER_SECTOR - 1) / FS_BYTES_PER_SECTOR;
    uint16_t c = d->first_cluster;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];
    uint32_t written = 0;

    for (uint32_t s = 0; s < sectors_needed; s++) {
        if (c < FS_FIRST_DATA_CLUSTER || c >= FS_CLUSTER_EOC_MIN) break; /* short/corrupt chain guard */

        disk_read_sector(cluster_to_lba(c), sector_buf);

        uint32_t remaining = size - written;
        uint32_t chunk = remaining < FS_BYTES_PER_SECTOR ? remaining : FS_BYTES_PER_SECTOR;
        for (uint32_t b = 0; b < chunk; b++) buffer[written + b] = sector_buf[b];

        written += chunk;
        c = fat_read(c);
    }

    if (out_size) *out_size = written;
    return 0;
}

void fs_list(void)
{
    int any = 0;
    char disp[FS_NAME_LEN];

    for (int i = 0; i < FS_ROOT_ENTRIES; i++) {
        uint8_t first = root[i].name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) continue;

        dirent_to_display(&root[i], disp);
        kprint("  ");
        kprint(disp);
        kprint("  (");
        kprint_hex32(root[i].file_size);
        kprint(" bytes)\n");
        any = 1;
    }
    if (!any) {
        kprint("  (no files)\n");
    }
}

int fs_delete(const char *name)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    int slot = find_slot(n, e);
    if (slot < 0) return -1;

    fs_dirent_t *d = &root[slot];
    if (d->first_cluster != 0) fat_free_chain(d->first_cluster);

    d->name[0] = FS_DIRENT_DELETED;
    d->first_cluster = 0;
    d->file_size = 0;

    root_save();
    return 0;
}

int fs_append(const char *name, const uint8_t *data, uint32_t extra_size)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    int slot = find_slot(n, e);
    if (slot < 0) {
        slot = fs_create(name);
        if (slot < 0) return -1;
    }
    fs_dirent_t *d = &root[slot];

    if (extra_size == 0) return 0;

    uint32_t cur = d->file_size;
    uint16_t first = d->first_cluster;
    uint16_t prev = 0; /* cluster to chain the next newly-allocated cluster from */
    uint32_t written = 0;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];

    if (cur > 0) {
        uint32_t last_index = (cur - 1) / FS_BYTES_PER_SECTOR;
        prev = cluster_at_index(first, last_index);
        if (!prev) return -1; /* corrupt chain */

        uint32_t offset_in_cluster = cur % FS_BYTES_PER_SECTOR;
        if (offset_in_cluster != 0) {
            /* top up the tail of the last cluster before allocating new ones */
            disk_read_sector(cluster_to_lba(prev), sector_buf);
            uint32_t space = FS_BYTES_PER_SECTOR - offset_in_cluster;
            uint32_t chunk = extra_size < space ? extra_size : space;
            for (uint32_t b = 0; b < chunk; b++) sector_buf[offset_in_cluster + b] = data[b];
            disk_write_sector(cluster_to_lba(prev), sector_buf);
            written += chunk;
        }
    }

    while (written < extra_size) {
        uint16_t c = fat_alloc_cluster();
        if (!c) {
            d->first_cluster = first;
            d->file_size = cur + written;
            root_save();
            return -1; /* disk full - whatever fit has been kept */
        }
        if (!first) first = c;
        else if (prev) fat_write(prev, c);
        prev = c;

        for (int b = 0; b < FS_BYTES_PER_SECTOR; b++) sector_buf[b] = 0;
        uint32_t remaining = extra_size - written;
        uint32_t chunk = remaining < FS_BYTES_PER_SECTOR ? remaining : FS_BYTES_PER_SECTOR;
        for (uint32_t b = 0; b < chunk; b++) sector_buf[b] = data[written + b];

        disk_write_sector(cluster_to_lba(c), sector_buf);
        written += chunk;
    }

    d->first_cluster = first;
    d->file_size = cur + written;
    root_save();
    return 0;
}

int fs_rename(const char *old_name, const char *new_name)
{
    uint8_t on[FS_NAME_FIELD_LEN], oe[FS_EXT_FIELD_LEN];
    split_name(old_name, on, oe);
    int slot = find_slot(on, oe);
    if (slot < 0) return -1;

    uint8_t nn[FS_NAME_FIELD_LEN], ne[FS_EXT_FIELD_LEN];
    split_name(new_name, nn, ne);
    if (find_slot(nn, ne) >= 0) return -1; /* target name already exists */

    fs_dirent_t *d = &root[slot];
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) d->name[i] = nn[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) d->ext[i] = ne[i];

    root_save();
    return 0;
}

int fs_stat(const char *name, uint32_t *out_size)
{
    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(name, n, e);

    int slot = find_slot(n, e);
    if (slot < 0) return -1;
    if (out_size) *out_size = root[slot].file_size;
    return 0;
}
