#include <stdint.h>
#include <stddef.h>
#include "scr/fs.h"
#include "scr/disk.h"

extern void kprint(const char *);
extern void kprint_hex32(uint32_t);

static fs_entry_t table[FS_MAX_FILES];

static void str_copy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void table_load(void)
{
    uint8_t buf[512];
    disk_read_sector(FS_TABLE_LBA, buf);

    fs_entry_t *entries = (fs_entry_t *)buf;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        table[i] = entries[i];
    }
}

static void table_save(void)
{
    uint8_t buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0;

    fs_entry_t *entries = (fs_entry_t *)buf;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        entries[i] = table[i];
    }

    disk_write_sector(FS_TABLE_LBA, buf);
}

void fs_init(void)
{
    uint8_t buf[512];
    disk_read_sector(FS_SUPERBLOCK_LBA, buf); /* superblock */

    uint32_t magic = *(uint32_t *)buf;

    if (magic != FS_MAGIC) {
        for (int i = 0; i < 512; i++) buf[i] = 0;
        *(uint32_t *)buf = FS_MAGIC;
        disk_write_sector(FS_SUPERBLOCK_LBA, buf);

        for (int i = 0; i < FS_MAX_FILES; i++) {
            table[i].used = 0;
        }
        table_save();

//        kprint("Filesystem formatted.\n");
    } else {
        table_load();
//        kprint("Filesystem loaded.\n");
    }
}

static int find_free_slot(void)
{
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!table[i].used) return i;
    }
    return -1;
}

static int find_file(const char *name)
{
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (table[i].used && str_eq(table[i].name, name)) return i;
    }
    return -1;
}

int fs_create(const char *name)
{
    if (find_file(name) >= 0) return -1; /* already exists */

    int slot = find_free_slot();
    if (slot < 0) return -1; /* table full */

    str_copy(table[slot].name, name, FS_NAME_LEN);
    table[slot].start_lba = FS_DATA_START_LBA + (uint32_t)slot * FS_MAX_FILE_SECTORS;
    table[slot].size_bytes = 0;
    table[slot].used = 1;

    table_save();
    return slot;
}

int fs_write(const char *name, const uint8_t *data, uint32_t size)
{
    int slot = find_file(name);
    if (slot < 0) {
        slot = fs_create(name);
        if (slot < 0) return -1;
    }

    uint32_t max_bytes = FS_MAX_FILE_SECTORS * 512;
    if (size > max_bytes) size = max_bytes; /* truncate, simple for now */

    uint32_t sectors_needed = (size + 511) / 512;
    uint8_t sector_buf[512];

    for (uint32_t s = 0; s < sectors_needed; s++) {
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;

        uint32_t remaining = size - s * 512;
        uint32_t chunk = remaining < 512 ? remaining : 512;

        for (uint32_t i = 0; i < chunk; i++) {
            sector_buf[i] = data[s * 512 + i];
        }

        disk_write_sector(table[slot].start_lba + s, sector_buf);
    }

    table[slot].size_bytes = size;
    table_save();
    return 0;
}

int fs_read(const char *name, uint8_t *buffer, uint32_t max_size, uint32_t *out_size)
{
    int slot = find_file(name);
    if (slot < 0) return -1;

    uint32_t size = table[slot].size_bytes;
    if (size > max_size) size = max_size;

    uint32_t sectors_needed = (size + 511) / 512;
    uint8_t sector_buf[512];

    for (uint32_t s = 0; s < sectors_needed; s++) {
        disk_read_sector(table[slot].start_lba + s, sector_buf);

        uint32_t remaining = size - s * 512;
        uint32_t chunk = remaining < 512 ? remaining : 512;

        for (uint32_t i = 0; i < chunk; i++) {
            buffer[s * 512 + i] = sector_buf[i];
        }
    }

    if (out_size) *out_size = size;
    return 0;
}

void fs_list(void)
{
    int any = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (table[i].used) {
            kprint("  ");
            kprint(table[i].name);
            kprint("  (");
            kprint_hex32(table[i].size_bytes);
            kprint(" bytes)\n");
            any = 1;
        }
    }
    if (!any) {
        kprint("  (no files)\n");
    }
}

int fs_delete(const char *name)
{
    int slot = find_file(name);
    if (slot < 0) return -1;

    table[slot].used = 0;
    table[slot].size_bytes = 0;
    table_save();
    return 0;
}

int fs_append(const char *name, const uint8_t *data, uint32_t extra_size)
{
    int slot = find_file(name);
    if (slot < 0) {
        slot = fs_create(name);
        if (slot < 0) return -1;
    }

    uint32_t current_size = table[slot].size_bytes;
    uint32_t new_size = current_size + extra_size;
    uint32_t max_bytes = FS_MAX_FILE_SECTORS * 512;

    if (new_size > max_bytes) {
        return -1; /* would overflow this file's fixed sector allocation */
    }

    /* read the existing sectors that contain the tail we're appending after,
     * since we can only write in whole 512-byte sectors */
    uint32_t start_sector = current_size / 512;
    uint32_t byte_offset_in_sector = current_size % 512;

    uint8_t sector_buf[512];
    uint32_t written = 0;

    while (written < extra_size) {
        uint32_t sector_index = start_sector + (byte_offset_in_sector + written) / 512;
        uint32_t offset_in_this_sector = (byte_offset_in_sector + written) % 512;

        /* load existing sector content first, so we don't clobber other bytes in it */
        disk_read_sector(table[slot].start_lba + sector_index, sector_buf);

        uint32_t space_left_in_sector = 512 - offset_in_this_sector;
        uint32_t remaining_to_write = extra_size - written;
        uint32_t chunk = remaining_to_write < space_left_in_sector ? remaining_to_write : space_left_in_sector;

        for (uint32_t i = 0; i < chunk; i++) {
            sector_buf[offset_in_this_sector + i] = data[written + i];
        }

        disk_write_sector(table[slot].start_lba + sector_index, sector_buf);

        written += chunk;
    }

    table[slot].size_bytes = new_size;
    table_save();
    return 0;
}

int fs_rename(const char *old_name, const char *new_name)
{
    int slot = find_file(old_name);
    if (slot < 0) return -1;
    if (find_file(new_name) >= 0) return -1;

    str_copy(table[slot].name, new_name, FS_NAME_LEN);
    table_save();
    return 0;
}

int fs_stat(const char *name, uint32_t *out_size)
{
    int slot = find_file(name);
    if (slot < 0) return -1;
    if (out_size) *out_size = table[slot].size_bytes;
    return 0;
}