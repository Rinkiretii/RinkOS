#ifndef FS_H
#define FS_H

#include <stdint.h>

/* ============================================================
 * RinkFS16 - a FAT16-style filesystem for RinkOS.
 *
 * Layout on disk (sector = 512 bytes):
 *
 *   LBA 0..49    bootloader + kernel (kernel currently ~50 sectors,
 *                boot.asm reserves room up to LBA 300)
 *   LBA 50..299  headroom for kernel growth
 *   LBA 300      boot sector / BPB
 *   LBA 301..363 FAT copy #1   (FS_FAT_SIZE_SECTORS sectors)
 *   LBA 364..426 FAT copy #2   (FS_FAT_SIZE_SECTORS sectors)
 *   LBA 427..434 root directory (FS_ROOT_ENTRIES * 32 bytes)
 *   LBA 435..end data area, addressed in clusters (cluster 2 = LBA 435)
 *
 * The volume now has ~15900 data clusters, comfortably above FAT's
 * traditional 4085-cluster FAT12/FAT16 cutoff - so unlike the original
 * 1.44MB image this is genuinely FAT16 by that convention too, not
 * just FAT16-shaped.
 *
 * These numbers are sized for the 1.44MB (2880-sector) image the
 * Makefile currently produces. If FS_IMAGE_TOTAL_SECTORS changes,
 * FS_FAT_SIZE_SECTORS must be recomputed so the FAT can still address
 * every data cluster (fat_size_sectors*512/2 >= data_clusters).
 *
 * Subdirectories: the root directory is the fixed area above and is
 * cached in RAM. Every other directory is an ordinary cluster chain
 * whose data is just an array of 32-byte entries (16 per cluster,
 * since a cluster is one sector here) - same as a real FAT16 volume.
 * A subdirectory's first two entries are "." (itself) and ".."
 * (its parent; first_cluster == 0 means "parent is root", since the
 * root has no cluster number of its own).
 * ============================================================ */

#define FS_IMAGE_TOTAL_SECTORS 16384  /* 8MB image, see Makefile truncate */

#define FS_VOL_START_LBA       300
#define FS_BOOT_LBA            (FS_VOL_START_LBA)

#define FS_BYTES_PER_SECTOR    512
#define FS_SECTORS_PER_CLUSTER 1      /* cluster == sector, keeps math simple */

#define FS_NUM_FATS             2
#define FS_FAT_SIZE_SECTORS    63     /* per copy - see layout note above */
#define FS_FAT1_LBA             (FS_VOL_START_LBA + 1)
#define FS_FAT2_LBA             (FS_FAT1_LBA + FS_FAT_SIZE_SECTORS)

#define FS_ROOT_ENTRIES         128
#define FS_ROOT_DIR_SECTORS     ((FS_ROOT_ENTRIES * 32) / FS_BYTES_PER_SECTOR) /* 8 */
#define FS_ROOT_LBA              (FS_FAT2_LBA + FS_FAT_SIZE_SECTORS)

#define FS_DATA_LBA              (FS_ROOT_LBA + FS_ROOT_DIR_SECTORS)
#define FS_FIRST_DATA_CLUSTER    2
#define FS_TOTAL_DATA_CLUSTERS   (FS_IMAGE_TOTAL_SECTORS - FS_DATA_LBA)

#define FS_ENTRIES_PER_CLUSTER   (FS_BYTES_PER_SECTOR / 32)  /* 16 */

/* FAT16 entry values (our own conventions, 16-bit) */
#define FS_CLUSTER_FREE          0x0000
#define FS_CLUSTER_RESERVED      0x0001
#define FS_CLUSTER_BAD           0xFFF7
#define FS_CLUSTER_EOC           0xFFFF   /* end-of-chain marker we write */
#define FS_CLUSTER_EOC_MIN       0xFFF8   /* anything >= this means "end" */

/* Directory entry flags (byte 0 of name[] / attr byte) */
#define FS_DIRENT_FREE           0x00
#define FS_DIRENT_DELETED        0xE5

/* Attribute bits - same positions as real FAT, though we only ever
 * set/check ATTR_DIRECTORY vs the plain-file default. */
#define FS_ATTR_NORMAL            0x20   /* DOS "archive" bit: plain file */
#define FS_ATTR_DIRECTORY         0x10

#define FS_NAME_FIELD_LEN        8
#define FS_EXT_FIELD_LEN         3

/* Scratch buffer size for a single path component (name.ext) fs.c
 * works with internally. */
#define FS_COMPONENT_LEN         13

/* Scratch buffer size shell.c uses when parsing a path token off the
 * command line. Generous on purpose - fs.c does the 8.3 splitting
 * and path-walking itself, the shell doesn't need to know details. */
#define FS_NAME_LEN              64

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

/* All of these accept a plain name ("readme.txt") or a path
 * ("docs/readme.txt", "/docs/readme.txt") - relative paths resolve
 * against the current directory (see fs_chdir). */
int fs_create(const char *path);
int fs_write(const char *path, const uint8_t *data, uint32_t size);
int fs_read(const char *path, uint8_t *buffer, uint32_t max_size, uint32_t *out_size);
int fs_delete(const char *path);
int fs_append(const char *path, const uint8_t *data, uint32_t extra_size);
int fs_rename(const char *old_path, const char *new_path);
int fs_stat(const char *path, uint32_t *out_size);

/* Lists the current directory (marks subdirectories as <DIR>). */
void fs_list(void);

/* Directory navigation. */
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_chdir(const char *path);
void fs_getcwd(char *buf, uint32_t max_len);

/* Volume stats, mainly for boot-time logging / a future "df". */
void fs_debug_stats(uint32_t *root_entries_used, uint32_t *free_clusters, uint32_t *total_clusters);

#endif
