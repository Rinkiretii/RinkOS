#include <stdint.h>
#include <stddef.h>
#include "scr/fs.h"
#include "scr/disk.h"

extern void kprint(const char *);
extern void kprint_hex32(uint32_t);
extern void kprint_uint(uint32_t);

/* Root directory, cached in RAM and flushed to disk on every change.
 * Every OTHER directory (a subdirectory) is just a normal cluster
 * chain read straight off disk on demand - see dir_ref_t below. */
static fs_dirent_t root[FS_ROOT_ENTRIES];

/* Current directory. is_root=1 means "the fixed root area"; otherwise
 * `cluster` is the first cluster of a subdirectory's entry array. */
static int cwd_is_root = 1;
static uint16_t cwd_cluster = 0;
static char cwd_path[128] = "/";

/* ---------- tiny string helpers (no libc available) ---------- */

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static void str_copy_n(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_eq_local(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Splits a user-typed "name.ext" (any case) into the space-padded,
 * uppercased 8.3 fields used on disk. */
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
 * directory entry, into a buffer of at least FS_COMPONENT_LEN bytes. */
static void dirent_to_display(const fs_dirent_t *d, char *out)
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

static const uint8_t DOT_NAME[FS_NAME_FIELD_LEN]    = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
static const uint8_t DOTDOT_NAME[FS_NAME_FIELD_LEN] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ' };
static const uint8_t EMPTY_EXT[FS_EXT_FIELD_LEN]    = { ' ', ' ', ' ' };

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

/* ============================================================
 * Directory abstraction.
 *
 * A directory is either "the root" (fixed area, cached in `root[]`)
 * or a subdirectory identified by its first cluster (a normal cluster
 * chain of 16-entries-per-cluster, read/written directly on disk).
 * Everything above this point doesn't know or care which; everything
 * below only talks to directories through dir_ref_t / entry_loc_t so
 * fs_create/fs_write/... etc don't need two code paths.
 * ============================================================ */

typedef struct {
    int is_root;
    uint16_t cluster; /* meaningful only if !is_root */
} dir_ref_t;

typedef struct {
    dir_ref_t dir;
    int root_index;        /* used if dir.is_root */
    uint16_t entry_cluster; /* used if !dir.is_root: cluster holding the entry */
    int entry_index;        /* used if !dir.is_root: 0..FS_ENTRIES_PER_CLUSTER-1 */
} entry_loc_t;

static dir_ref_t cwd_dir(void)
{
    dir_ref_t d;
    d.is_root = cwd_is_root;
    d.cluster = cwd_cluster;
    return d;
}

typedef struct {
    dir_ref_t dir;
    int root_index;
    uint16_t cluster;
    int in_cluster_index;
    int done;
} dir_iter_t;

static void dir_iter_start(dir_ref_t dir, dir_iter_t *it)
{
    it->dir = dir;
    it->done = 0;
    if (dir.is_root) {
        it->root_index = 0;
    } else {
        it->cluster = dir.cluster;
        it->in_cluster_index = 0;
    }
}

/* Advances the iterator, filling *out and *loc. Returns 0 at the end. */
static int dir_iter_next(dir_iter_t *it, fs_dirent_t *out, entry_loc_t *loc)
{
    if (it->done) return 0;

    if (it->dir.is_root) {
        if (it->root_index >= FS_ROOT_ENTRIES) { it->done = 1; return 0; }
        *out = root[it->root_index];
        loc->dir = it->dir;
        loc->root_index = it->root_index;
        it->root_index++;
        return 1;
    }

    if (it->cluster < FS_FIRST_DATA_CLUSTER || it->cluster >= FS_CLUSTER_EOC_MIN) {
        it->done = 1;
        return 0;
    }

    uint8_t buf[FS_BYTES_PER_SECTOR];
    disk_read_sector(cluster_to_lba(it->cluster), buf);
    fs_dirent_t *entries = (fs_dirent_t *)buf;

    *out = entries[it->in_cluster_index];
    loc->dir = it->dir;
    loc->entry_cluster = it->cluster;
    loc->entry_index = it->in_cluster_index;

    it->in_cluster_index++;
    if (it->in_cluster_index >= FS_ENTRIES_PER_CLUSTER) {
        it->in_cluster_index = 0;
        it->cluster = fat_read(it->cluster);
    }
    return 1;
}

static void dir_write_entry(const entry_loc_t *loc, const fs_dirent_t *entry)
{
    if (loc->dir.is_root) {
        root[loc->root_index] = *entry;
        root_save();
        return;
    }

    uint8_t buf[FS_BYTES_PER_SECTOR];
    uint32_t lba = cluster_to_lba(loc->entry_cluster);
    disk_read_sector(lba, buf);
    fs_dirent_t *entries = (fs_dirent_t *)buf;
    entries[loc->entry_index] = *entry;
    disk_write_sector(lba, buf);
}

static int dir_scan(dir_ref_t dir, const uint8_t name8[FS_NAME_FIELD_LEN], const uint8_t ext3[FS_EXT_FIELD_LEN],
                     fs_dirent_t *out, entry_loc_t *loc)
{
    dir_iter_t it;
    dir_iter_start(dir, &it);
    fs_dirent_t e;
    entry_loc_t l;
    while (dir_iter_next(&it, &e, &l)) {
        uint8_t first = e.name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) continue;
        if (fields_eq(e.name, name8, FS_NAME_FIELD_LEN) && fields_eq(e.ext, ext3, FS_EXT_FIELD_LEN)) {
            if (out) *out = e;
            if (loc) *loc = l;
            return 1;
        }
    }
    return 0;
}

/* Finds a free/deleted slot in `dir`. For a subdirectory, if every
 * existing cluster is full, a new cluster is allocated and chained on
 * automatically (this is exactly how a real FAT16 directory grows). */
static int dir_find_free(dir_ref_t dir, entry_loc_t *loc)
{
    dir_iter_t it;
    dir_iter_start(dir, &it);
    fs_dirent_t e;
    entry_loc_t l;
    uint16_t last_cluster = dir.is_root ? 0 : dir.cluster;

    while (dir_iter_next(&it, &e, &l)) {
        if (!dir.is_root) last_cluster = l.entry_cluster;
        uint8_t first = e.name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) {
            *loc = l;
            return 0;
        }
    }

    if (dir.is_root) return -1; /* root can't grow */

    uint16_t new_cluster = fat_alloc_cluster();
    if (!new_cluster) return -1; /* disk full */
    fat_write(last_cluster, new_cluster);

    uint8_t buf[FS_BYTES_PER_SECTOR];
    for (int i = 0; i < FS_BYTES_PER_SECTOR; i++) buf[i] = 0;
    disk_write_sector(cluster_to_lba(new_cluster), buf);

    loc->dir = dir;
    loc->entry_cluster = new_cluster;
    loc->entry_index = 0;
    return 0;
}

/* ---------- path walking ---------- */

/* Appends "/name" (from a matched dirent) to a cwd-style path buffer. */
static void path_append(char *path_buf, int buf_len, const fs_dirent_t *entry)
{
    int len = 0;
    while (path_buf[len]) len++;

    char disp[FS_COMPONENT_LEN];
    dirent_to_display(entry, disp);

    if (len > 1 || (len == 1 && path_buf[0] != '/')) {
        if (len < buf_len - 1) path_buf[len++] = '/';
    } else if (len == 0) {
        if (len < buf_len - 1) path_buf[len++] = '/';
    }
    /* if path_buf is exactly "/", don't add a second slash */
    int i = 0;
    while (disp[i] && len < buf_len - 1) { path_buf[len++] = disp[i]; i++; }
    path_buf[len] = '\0';
}

static void path_trim_last(char *path_buf)
{
    int len = 0;
    while (path_buf[len]) len++;
    if (len <= 1) return; /* already "/" or empty */

    len--;
    while (len > 0 && path_buf[len] != '/') len--;
    if (len == 0) len = 1; /* keep the leading "/" */
    path_buf[len] = '\0';
}

/* Walks `path` starting from *dir (updating it in place), optionally
 * updating a cwd-style display path alongside (pass NULL to skip).
 * A leading '/' in `path` resets *dir to the volume root. Returns 0
 * on success, -1 if a component doesn't exist or isn't a directory. */
static int walk_path(const char *path, dir_ref_t *dir, char *path_buf, int path_buf_len)
{
    const char *p = path;

    if (*p == '/') {
        dir->is_root = 1;
        dir->cluster = 0;
        if (path_buf) { path_buf[0] = '/'; path_buf[1] = '\0'; }
        p++;
    }

    while (*p) {
        char comp[FS_COMPONENT_LEN];
        int i = 0;
        while (p[i] && p[i] != '/' && i < FS_COMPONENT_LEN - 1) { comp[i] = p[i]; i++; }
        comp[i] = '\0';

        p += i;
        while (*p == '/') p++;

        if (comp[0] == '\0' || str_eq_local(comp, ".")) continue;

        if (str_eq_local(comp, "..")) {
            if (!dir->is_root) {
                fs_dirent_t e;
                entry_loc_t loc;
                if (dir_scan(*dir, DOTDOT_NAME, EMPTY_EXT, &e, &loc)) {
                    if (e.first_cluster == 0) dir->is_root = 1;
                    else { dir->is_root = 0; dir->cluster = e.first_cluster; }
                }
            }
            if (path_buf) path_trim_last(path_buf);
            continue;
        }

        uint8_t n[FS_NAME_FIELD_LEN], e3[FS_EXT_FIELD_LEN];
        split_name(comp, n, e3);

        fs_dirent_t entry;
        entry_loc_t loc;
        if (!dir_scan(*dir, n, e3, &entry, &loc)) return -1;
        if (!(entry.attr & FS_ATTR_DIRECTORY)) return -1;

        dir->is_root = 0;
        dir->cluster = entry.first_cluster;
        if (path_buf) path_append(path_buf, path_buf_len, &entry);
    }
    return 0;
}

/* Splits `path` into (parent directory, leaf component). Doesn't
 * check whether the leaf itself exists - callers do that. */
static int resolve_parent(const char *path, dir_ref_t *out_dir, char *leaf_out)
{
    if (path[0] == '\0') return -1;

    int last_slash = -1;
    int len = 0;
    while (path[len]) { if (path[len] == '/') last_slash = len; len++; }

    if (last_slash < 0) {
        str_copy_n(leaf_out, path, FS_COMPONENT_LEN);
        *out_dir = cwd_dir();
        return 0;
    }

    str_copy_n(leaf_out, path + last_slash + 1, FS_COMPONENT_LEN);

    char dirpart[96];
    int dl = last_slash + 1; /* include the slash itself */
    if (dl > (int)sizeof(dirpart) - 1) dl = sizeof(dirpart) - 1;
    for (int i = 0; i < dl; i++) dirpart[i] = path[i];
    dirpart[dl] = '\0';

    dir_ref_t d = cwd_dir();
    if (walk_path(dirpart, &d, NULL, 0) < 0) return -1;
    *out_dir = d;
    return 0;
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

//        kprint("[fs] no RinkFS16 volume found - formatted a fresh one\n");
    } else {
        root_load();
//        kprint("[fs] RinkFS16 volume found - mounted\n");
    }

    cwd_is_root = 1;
    cwd_cluster = 0;
    cwd_path[0] = '/';
    cwd_path[1] = '\0';
}

int fs_create(const char *path)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;
    if (leaf[0] == '\0') return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    if (dir_scan(parent, n, e, NULL, NULL)) return -1; /* already exists */

    entry_loc_t loc;
    if (dir_find_free(parent, &loc) < 0) return -1; /* directory full / disk full */

    fs_dirent_t d;
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) d.name[i] = n[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) d.ext[i] = e[i];
    d.attr = FS_ATTR_NORMAL;
    for (int i = 0; i < 10; i++) d.reserved[i] = 0;
    d.time = 0;
    d.date = 0;
    d.first_cluster = 0;
    d.file_size = 0;

    dir_write_entry(&loc, &d);
    return 0;
}

int fs_write(const char *path, const uint8_t *data, uint32_t size)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;
    if (leaf[0] == '\0') return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    fs_dirent_t d;
    entry_loc_t loc;
    int existed = dir_scan(parent, n, e, &d, &loc);

    if (!existed) {
        if (fs_create(path) < 0) return -1;
        if (!dir_scan(parent, n, e, &d, &loc)) return -1;
    }
    if (d.attr & FS_ATTR_DIRECTORY) return -1; /* can't write to a directory */

    if (d.first_cluster != 0) {
        fat_free_chain(d.first_cluster);
        d.first_cluster = 0;
    }
    d.file_size = 0;

    if (size == 0) {
        dir_write_entry(&loc, &d);
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
            d.first_cluster = 0;
            d.file_size = 0;
            dir_write_entry(&loc, &d);
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

    d.first_cluster = first;
    d.file_size = size;
    dir_write_entry(&loc, &d);
    return 0;
}

int fs_read(const char *path, uint8_t *buffer, uint32_t max_size, uint32_t *out_size)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    fs_dirent_t d;
    if (!dir_scan(parent, n, e, &d, NULL)) return -1;
    if (d.attr & FS_ATTR_DIRECTORY) return -1;

    uint32_t size = d.file_size;
    if (size > max_size) size = max_size;

    uint32_t sectors_needed = (size + FS_BYTES_PER_SECTOR - 1) / FS_BYTES_PER_SECTOR;
    uint16_t c = d.first_cluster;
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
    char disp[FS_COMPONENT_LEN];

    dir_iter_t it;
    dir_iter_start(cwd_dir(), &it);
    fs_dirent_t e;
    entry_loc_t loc;

    while (dir_iter_next(&it, &e, &loc)) {
        uint8_t first = e.name[0];
        if (first == FS_DIRENT_FREE || first == FS_DIRENT_DELETED) continue;
        if (fields_eq(e.name, DOT_NAME, FS_NAME_FIELD_LEN)) continue;
        if (fields_eq(e.name, DOTDOT_NAME, FS_NAME_FIELD_LEN)) continue;

        dirent_to_display(&e, disp);
        kprint("  ");
        kprint(disp);
        if (e.attr & FS_ATTR_DIRECTORY) {
            kprint("  <DIR>\n");
        } else {
            kprint("  (");
            kprint_hex32(e.file_size);
            kprint(" bytes)\n");
        }
        any = 1;
    }
    if (!any) {
        kprint("  (empty)\n");
    }
}

int fs_delete(const char *path)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    fs_dirent_t d;
    entry_loc_t loc;
    if (!dir_scan(parent, n, e, &d, &loc)) return -1;
    if (d.attr & FS_ATTR_DIRECTORY) return -1; /* use a dedicated rmdir for directories */

    if (d.first_cluster != 0) fat_free_chain(d.first_cluster);

    d.name[0] = FS_DIRENT_DELETED;
    d.first_cluster = 0;
    d.file_size = 0;
    dir_write_entry(&loc, &d);
    return 0;
}

int fs_append(const char *path, const uint8_t *data, uint32_t extra_size)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;
    if (leaf[0] == '\0') return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    fs_dirent_t d;
    entry_loc_t loc;
    int existed = dir_scan(parent, n, e, &d, &loc);
    if (!existed) {
        if (fs_create(path) < 0) return -1;
        if (!dir_scan(parent, n, e, &d, &loc)) return -1;
    }
    if (d.attr & FS_ATTR_DIRECTORY) return -1;

    if (extra_size == 0) return 0;

    uint32_t cur = d.file_size;
    uint16_t first = d.first_cluster;
    uint16_t prev = 0; /* cluster to chain the next newly-allocated cluster from */
    uint32_t written = 0;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];

    if (cur > 0) {
        uint32_t last_index = (cur - 1) / FS_BYTES_PER_SECTOR;
        prev = cluster_at_index(first, last_index);
        if (!prev) return -1; /* corrupt chain */

        uint32_t offset_in_cluster = cur % FS_BYTES_PER_SECTOR;
        if (offset_in_cluster != 0) {
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
            d.first_cluster = first;
            d.file_size = cur + written;
            dir_write_entry(&loc, &d);
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

    d.first_cluster = first;
    d.file_size = cur + written;
    dir_write_entry(&loc, &d);
    return 0;
}

int fs_rename(const char *old_path, const char *new_path)
{
    dir_ref_t old_parent;
    char old_leaf[FS_COMPONENT_LEN];
    if (resolve_parent(old_path, &old_parent, old_leaf) < 0) return -1;

    uint8_t on[FS_NAME_FIELD_LEN], oe[FS_EXT_FIELD_LEN];
    split_name(old_leaf, on, oe);

    fs_dirent_t d;
    entry_loc_t loc;
    if (!dir_scan(old_parent, on, oe, &d, &loc)) return -1;

    dir_ref_t new_parent;
    char new_leaf[FS_COMPONENT_LEN];
    if (resolve_parent(new_path, &new_parent, new_leaf) < 0) return -1;
    if (new_leaf[0] == '\0') return -1;

    uint8_t nn[FS_NAME_FIELD_LEN], ne[FS_EXT_FIELD_LEN];
    split_name(new_leaf, nn, ne);

    /* only same-directory renames are supported for now */
    if (new_parent.is_root != old_parent.is_root || new_parent.cluster != old_parent.cluster) {
        return -1;
    }
    if (dir_scan(new_parent, nn, ne, NULL, NULL)) return -1; /* target already exists */

    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) d.name[i] = nn[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) d.ext[i] = ne[i];
    dir_write_entry(&loc, &d);
    return 0;
}

int fs_stat(const char *path, uint32_t *out_size)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    fs_dirent_t d;
    if (!dir_scan(parent, n, e, &d, NULL)) return -1;
    if (out_size) *out_size = d.file_size;
    return 0;
}

int fs_mkdir(const char *path)
{
    dir_ref_t parent;
    char leaf[FS_COMPONENT_LEN];
    if (resolve_parent(path, &parent, leaf) < 0) return -1;
    if (leaf[0] == '\0') return -1;
    if (str_eq_local(leaf, ".") || str_eq_local(leaf, "..")) return -1;

    uint8_t n[FS_NAME_FIELD_LEN], e[FS_EXT_FIELD_LEN];
    split_name(leaf, n, e);

    if (dir_scan(parent, n, e, NULL, NULL)) return -1; /* already exists */

    uint16_t new_cluster = fat_alloc_cluster();
    if (!new_cluster) return -1;

    uint8_t buf[FS_BYTES_PER_SECTOR];
    for (int i = 0; i < FS_BYTES_PER_SECTOR; i++) buf[i] = 0;
    disk_write_sector(cluster_to_lba(new_cluster), buf);

    fs_dirent_t dot;
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) dot.name[i] = DOT_NAME[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) dot.ext[i] = EMPTY_EXT[i];
    dot.attr = FS_ATTR_DIRECTORY;
    for (int i = 0; i < 10; i++) dot.reserved[i] = 0;
    dot.time = 0; dot.date = 0;
    dot.first_cluster = new_cluster;
    dot.file_size = 0;

    fs_dirent_t dotdot = dot;
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) dotdot.name[i] = DOTDOT_NAME[i];
    dotdot.first_cluster = parent.is_root ? 0 : parent.cluster;

    entry_loc_t loc0 = { .dir = { 0, new_cluster }, .entry_cluster = new_cluster, .entry_index = 0 };
    entry_loc_t loc1 = { .dir = { 0, new_cluster }, .entry_cluster = new_cluster, .entry_index = 1 };
    dir_write_entry(&loc0, &dot);
    dir_write_entry(&loc1, &dotdot);

    entry_loc_t free_loc;
    if (dir_find_free(parent, &free_loc) < 0) {
        fat_free_chain(new_cluster);
        return -1;
    }

    fs_dirent_t new_entry;
    for (int i = 0; i < FS_NAME_FIELD_LEN; i++) new_entry.name[i] = n[i];
    for (int i = 0; i < FS_EXT_FIELD_LEN; i++) new_entry.ext[i] = e[i];
    new_entry.attr = FS_ATTR_DIRECTORY;
    for (int i = 0; i < 10; i++) new_entry.reserved[i] = 0;
    new_entry.time = 0; new_entry.date = 0;
    new_entry.first_cluster = new_cluster;
    new_entry.file_size = 0;
    dir_write_entry(&free_loc, &new_entry);

    return 0;
}

int fs_chdir(const char *path)
{
    dir_ref_t d = cwd_dir();
    char buf[sizeof(cwd_path)];
    str_copy_n(buf, cwd_path, sizeof(buf));

    if (walk_path(path, &d, buf, sizeof(buf)) < 0) return -1;

    cwd_is_root = d.is_root;
    cwd_cluster = d.cluster;
    str_copy_n(cwd_path, buf, sizeof(cwd_path));
    return 0;
}

void fs_getcwd(char *buf, uint32_t max_len)
{
    str_copy_n(buf, cwd_path, (int)max_len);
}

void fs_debug_stats(uint32_t *root_entries_used, uint32_t *free_clusters, uint32_t *total_clusters)
{
    if (root_entries_used) {
        uint32_t used = 0;
        for (int i = 0; i < FS_ROOT_ENTRIES; i++) {
            uint8_t first = root[i].name[0];
            if (first != FS_DIRENT_FREE && first != FS_DIRENT_DELETED) used++;
        }
        *root_entries_used = used;
    }
    if (free_clusters) {
        uint32_t free_count = 0;
        for (uint16_t c = FS_FIRST_DATA_CLUSTER; c < FS_FIRST_DATA_CLUSTER + FS_TOTAL_DATA_CLUSTERS; c++) {
            if (fat_read(c) == FS_CLUSTER_FREE) free_count++;
        }
        *free_clusters = free_count;
    }
    if (total_clusters) {
        *total_clusters = FS_TOTAL_DATA_CLUSTERS;
    }
}
