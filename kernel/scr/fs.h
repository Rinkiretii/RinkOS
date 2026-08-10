#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_MAGIC          0x52494E4B
#define FS_MAX_FILES      16
#define FS_NAME_LEN       12
#define FS_SUPERBLOCK_LBA  100
#define FS_TABLE_LBA      101
#define FS_DATA_START_LBA 103  /* leave room, table might span a bit more than 1 sector with padding */
#define FS_MAX_FILE_SECTORS 8 /* cap file size at 8 sectors = 4KB for now */

typedef struct __attribute__((packed)) {
    char name[FS_NAME_LEN];
    uint32_t start_lba;
    uint32_t size_bytes;
    uint8_t used;
} fs_entry_t;

void fs_init(void);
int fs_create(const char *name);
int fs_write(const char *name, const uint8_t *data, uint32_t size);
int fs_read(const char *name, uint8_t *buffer, uint32_t max_size, uint32_t *out_size);
void fs_list(void);
int fs_delete(const char *name);
int fs_append(const char *name, const uint8_t *data, uint32_t extra_size);
int fs_stat(const char *name, uint32_t *out_size);

#endif