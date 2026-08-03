#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 4
#define TASK_STACK_SIZE 4096

typedef struct {
    uint32_t esp;       /* saved stack pointer when this task isn't running */
    int used;
} task_t;

void tasks_init(void);
void task_create(void (*entry)(void));
void schedule(uint32_t *old_esp_store); /* called from timer IRQ */

#endif