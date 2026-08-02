#ifndef MMAP_H
#define MMAP_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} mmap_entry_t;

#define MMAP_ADDR  0x9000
#define MMAP_COUNT (*(uint16_t *)0x8FFC)

void mmap_dump(void);   /* <-- make sure this line is present */

#endif