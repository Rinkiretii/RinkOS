#include <stdint.h>
#include <stddef.h>
#include "scr/task.h"
#include "scr/mm.h"

static task_t tasks[MAX_TASKS];
static int current_task = -1;
static int task_count = 0;

extern void task_switch(uint32_t *old_esp, uint32_t new_esp);

void tasks_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].used = 0;
    }
    current_task = -1;
    task_count = 0;
}

void task_create(void (*entry)(void))
{
    if (task_count >= MAX_TASKS) {
        return; /* no room */
    }

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    uint32_t *sp = (uint32_t *)(stack + TASK_STACK_SIZE);

    /* build a fake stack frame so our context-switch code, written
     * as a normal function, can "return" into the new task via ret */
    sp -= 1; *sp = (uint32_t)entry;   /* return address: task entry point */
    sp -= 1; *sp = 0;                 /* ebp */
    sp -= 1; *sp = 0;                 /* ebx */
    sp -= 1; *sp = 0;                 /* esi */
    sp -= 1; *sp = 0;                 /* edi */

    int slot = task_count++;
    tasks[slot].esp = (uint32_t)sp;
    tasks[slot].used = 1;
}

void schedule(uint32_t *old_esp_store)
{
    (void)old_esp_store;

    if (task_count == 0) {
        return;
    }

    int next = current_task;
    do {
        next = (next + 1) % task_count;
    } while (!tasks[next].used);

    if (current_task == -1) {
        current_task = next;
        uint32_t dummy;
        task_switch(&dummy, tasks[next].esp);
        return;
    }

    if (next == current_task) {
        return;
    }

    int prev = current_task;
    current_task = next;

    task_switch(&tasks[prev].esp, tasks[next].esp);
}