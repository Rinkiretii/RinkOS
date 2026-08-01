#include <stdint.h>
#include <stddef.h>
#include "scr/mm.h"

/* provided by linker.ld — marks the end of the kernel's .bss section */
extern uint8_t _bss_end;

#define HEAP_START ((uint32_t)&_bss_end)
#define HEAP_END   0x80000u   /* stay well clear of the 0x90000 stack */

typedef struct block_header {
    size_t size;               /* usable size, not counting this header */
    int free;
    struct block_header *next;
} block_header_t;

static block_header_t *heap_start = NULL;

void kmalloc_init(void)
{
    heap_start = (block_header_t *)HEAP_START;
    heap_start->size = HEAP_END - HEAP_START - sizeof(block_header_t);
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