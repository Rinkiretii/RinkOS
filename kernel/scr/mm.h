#ifndef MM_H
#define MM_H

#include <stddef.h>

void kmalloc_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void kmalloc_stats(size_t *total, size_t *used, size_t *free_bytes);

#endif /* MM_H */