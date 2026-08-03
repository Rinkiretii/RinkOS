#include <stdint.h>
#include <stddef.h>
#include "scr/mm.h"
#include "scr/mmap.h"

/* provided by linker.ld — marks the end of the kernel's .bss section */
extern uint8_t _bss_end;
extern void kprint(const char *);
extern void kprint_hex32(uint32_t);

#define HEAP_START ((uint32_t)&_bss_end)
#define HEAP_END   0x80000u   /* stay well clear of the 0x90000 stack */

uint32_t find_heap_end(uint32_t heap_start)
{
    mmap_entry_t *entries = (mmap_entry_t *)MMAP_ADDR;
    uint16_t count = MMAP_COUNT;

    uint32_t best_end = heap_start + 0x10000; /* fallback: 64KB if map is empty */

    for (uint16_t i = 0; i < count; i++) {
        uint64_t base = entries[i].base;
        uint64_t len  = entries[i].length;
        uint32_t type = entries[i].type;

        if (type != 1) continue;              /* skip non-usable regions */
        if (base > 0xFFFFFFFFu) continue;      /* ignore >4GB regions, we're 32-bit */

        uint32_t region_start = (uint32_t)base;
        uint32_t region_end   = (uint32_t)(base + len);

        if (region_start <= heap_start && region_end > heap_start) {
            if (region_end > best_end) {
                best_end = region_end;
            }
        }
    }

    /* stay safely below the 0x90000 stack regardless of what the map says */
    if (best_end > 0x90000) {
        best_end = 0x90000;
    }

    return best_end;
}

typedef struct block_header {
    size_t size;               /* usable size, not counting this header */
    int free;
    struct block_header *next;
} block_header_t;

static block_header_t *heap_start = NULL;

void kmalloc_init(void)
{
    heap_start = (block_header_t *)HEAP_START;
    uint32_t heap_end = find_heap_end(HEAP_START);

    heap_start->size = heap_end - HEAP_START - sizeof(block_header_t);
    heap_start->free = 1;
    heap_start->next = NULL;
}

static void split_block(block_header_t *block, size_t size)
{
    /* only split if the remainder is big enough to be worth its own header */
    if (block->size >= size + sizeof(block_header_t) + 4) {
        block_header_t *new_block =
            (block_header_t *)((uint8_t *)block + sizeof(block_header_t) + size);

        new_block->size = block->size - size - sizeof(block_header_t);
        new_block->free = 1;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;
    }
}

void *kmalloc(size_t size)
{
    if (size == 0 || heap_start == NULL) {
        return NULL;
    }

    size = (size + 3) & ~((size_t)3);   /* 4-byte align */

    block_header_t *curr = heap_start;
    while (curr) {
        if (curr->free && curr->size >= size) {
            split_block(curr, size);
            curr->free = 0;
            return (void *)((uint8_t *)curr + sizeof(block_header_t));
        }
        curr = curr->next;
    }

    return NULL;   /* out of memory */
}

static void coalesce(void)
{
    block_header_t *curr = heap_start;
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void kfree(void *ptr)
{
    if (!ptr) {
        return;
    }
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    block->free = 1;
    coalesce();
}

void kmalloc_stats(size_t *total, size_t *used, size_t *free_bytes)
{
    size_t t = 0, u = 0, f = 0;
    block_header_t *curr = heap_start;

    while (curr) {
        t += curr->size;
        if (curr->free) {
            f += curr->size;
        } else {
            u += curr->size;
        }
        curr = curr->next;
    }

    if (total) *total = t;
    if (used) *used = u;
    if (free_bytes) *free_bytes = f;
}

void mmap_dump(void)
{
    mmap_entry_t *entries = (mmap_entry_t *)MMAP_ADDR;
    uint16_t count = MMAP_COUNT;

    kprint("Memory map entries: ");
    kprint_hex32(count);
    kprint("\n");

    for (uint16_t i = 0; i < count; i++) {
        kprint("  base=");
        kprint_hex32((uint32_t)entries[i].base);
        kprint(" len=");
        kprint_hex32((uint32_t)entries[i].length);
        kprint(" type=");
        kprint_hex32(entries[i].type);
        kprint("\n");
    }
}
