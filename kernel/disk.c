#include <stdint.h>
#include "scr/io.h"
#include "scr/disk.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30

static void ata_wait_ready(void)
{
    while (inb(ATA_STATUS) & 0x80) { } /* wait while BSY set */
}

static void ata_wait_drq(void)
{
    while (!(inb(ATA_STATUS) & 0x08)) { } /* wait until DRQ set */
}

int disk_read_sector(uint32_t lba, uint8_t *buffer)
{
    ata_wait_ready();

    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_wait_ready();
    ata_wait_drq();

    uint16_t *buf16 = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(ATA_DATA);
    }

    return 0;
}

int disk_write_sector(uint32_t lba, const uint8_t *buffer)
{
    ata_wait_ready();

    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_wait_ready();
    ata_wait_drq();

    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_DATA, buf16[i]);
    }

    ata_wait_ready();

    /* flush the drive's write cache so the write is actually durable */
    outb(ATA_COMMAND, 0xE7); /* CACHE FLUSH */
    ata_wait_ready();

    return 0;
}