#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 4
#define TASK_STACK_SIZE 16384

typedef struct {
    uint32_t esp;       /* saved stack pointer when this task isn't running */
    int used;
    char name[16];
} task_t;

void tasks_init(void);
void task_create(void (*entry)(void));
void schedule(uint32_t *old_esp_store); /* called from timer IRQ */

void task_create_named(void (*entry)(void), const char *name);
void task_list(void);

#endif