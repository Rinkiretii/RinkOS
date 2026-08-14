#ifndef FS_H
#define FS_H

#include <stdint.h>

/* ============================================================
 * RinkFS16 - a FAT16-style filesystem for RinkOS.
 *
 * Layout on disk (sector = 512 bytes):
 *
 *   LBA 0..49    bootloader + kernel (untouched by this driver)
 *   LBA 50..99   reserved / headroom for kernel growth
 *   LBA 100      boot sector / BPB
 *   LBA 101..111 FAT copy #1   (FS_FAT_SIZE_SECTORS sectors)
 *   LBA 112..122 FAT copy #2   (FS_FAT_SIZE_SECTORS sectors)
 *   LBA 123..130 root directory (FS_ROOT_ENTRIES * 32 bytes)
 *   LBA 131..end data area, addressed in clusters (cluster 2 = LBA 131)
 *
 * These numbers are sized for the 1.44MB (2880-sector) image the
 * Makefile currently produces. If FS_IMAGE_TOTAL_SECTORS changes,
 * FS_FAT_SIZE_SECTORS must be recomputed so the FAT can still address
 * every data cluster (fat_size_sectors*512/2 >= data_clusters).
 * ============================================================ */

#define FS_IMAGE_TOTAL_SECTORS 2880   /* 1.44MB image, see Makefile truncate */

#define FS_VOL_START_LBA       100
#define FS_BOOT_LBA            (FS_VOL_START_LBA)

#define FS_BYTES_PER_SECTOR    512
#define FS_SECTORS_PER_CLUSTER 1      /* cluster == sector, keeps math simple */

#define FS_NUM_FATS             2
#define FS_FAT_SIZE_SECTORS    11     /* per copy - see layout note above */
#define FS_FAT1_LBA             (FS_VOL_START_LBA + 1)
#define FS_FAT2_LBA             (FS_FAT1_LBA + FS_FAT_SIZE_SECTORS)

#define FS_ROOT_ENTRIES         128
#define FS_ROOT_DIR_SECTORS     ((FS_ROOT_ENTRIES * 32) / FS_BYTES_PER_SECTOR) /* 8 */
#define FS_ROOT_LBA             (FS_FAT2_LBA + FS_FAT_SIZE_SECTORS)

#define FS_DATA_LBA             (FS_ROOT_LBA + FS_ROOT_DIR_SECTORS)
#define FS_FIRST_DATA_CLUSTER   2
#define FS_TOTAL_DATA_CLUSTERS  (FS_IMAGE_TOTAL_SECTORS - FS_DATA_LBA)

/* FAT16 entry values (our own conventions, 16-bit) */
#define FS_CLUSTER_FREE         0x0000
#define FS_CLUSTER_RESERVED     0x0001
#define FS_CLUSTER_BAD          0xFFF7
#define FS_CLUSTER_EOC          0xFFFF   /* end-of-chain marker we write */
#define FS_CLUSTER_EOC_MIN      0xFFF8   /* anything >= this means "end" */

/* Directory entry flags (byte 0 of name[] / attr byte) */
#define FS_DIRENT_FREE          0x00
#define FS_DIRENT_DELETED       0xE5
#define FS_ATTR_NORMAL          0x20     /* DOS "archive" bit, just a marker here */

#define FS_NAME_FIELD_LEN       8
#define FS_EXT_FIELD_LEN        3

/* Scratch buffer size shell.c uses when parsing a filename token off the
 * command line. Longer than the 8.3 on-disk fields on purpose - fs.c does
 * the truncation/uppercasing itself when it splits name.ext, so the shell
 * doesn't need to know FAT 8.3 details. */
#define FS_NAME_LEN              32

typedef struct __attribute__((packed)) {
    uint8_t  name[FS_NAME_FIELD_LEN]; /* space-padded, uppercase */
    uint8_t  ext[FS_EXT_FIELD_LEN];   /* space-padded, uppercase */
    uint8_t  attr;
    uint8_t  reserved[10];
    uint16_t time;                    /* unused, kept for real-BPB-shaped layout */
    uint16_t date;                    /* unused */
    uint16_t first_cluster;
    uint32_t file_size;
} fs_dirent_t;

void fs_init(void);
int fs_create(const char *name);
int fs_write(const char *name, const uint8_t *data, uint32_t size);
int fs_read(const char *name, uint8_t *buffer, uint32_t max_size, uint32_t *out_size);
void fs_list(void);
int fs_delete(const char *name);
int fs_append(const char *name, const uint8_t *data, uint32_t extra_size);
int fs_rename(const char *old_name, const char *new_name);
int fs_stat(const char *name, uint32_t *out_size);

#endif
