#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 16384

typedef struct {
    uint32_t esp;        /* saved stack pointer when this task isn't running */
    int used;
    char name[16];
    uint8_t *stack_base; /* base of the kmalloc'd stack, needed to free it on kill */
} task_t;

void tasks_init(void);
void task_create(void (*entry)(void));
void schedule(uint32_t *old_esp_store); /* called from timer IRQ */

/* Returns the new task's id (>= 0) on success, -1 if MAX_TASKS is reached. */
int task_create_named(void (*entry)(void), const char *name);
void task_list(void);

/* Terminates task `id` and frees its stack. Refuses (-1) if the id is
 * invalid, already dead, or is the task calling task_kill itself -
 * this scheduler has no safe way to tear down its own running stack
 * mid-instruction, so self-kill is simply not allowed. */
int task_kill(int id);
int task_current_id(void);

#endif