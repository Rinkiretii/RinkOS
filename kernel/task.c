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

static void name_copy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

int task_create_named(void (*entry)(void), const char *name)
{
    if (task_count >= MAX_TASKS) return -1;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    uint32_t *sp = (uint32_t *)(stack + TASK_STACK_SIZE);

    sp -= 1; *sp = (uint32_t)entry;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0;

    int slot = task_count++;
    tasks[slot].esp = (uint32_t)sp;
    tasks[slot].used = 1;
    tasks[slot].stack_base = stack;
    name_copy(tasks[slot].name, name, 16);
    return slot;
}


/* keep old task_create for compatibility, just call the named version */
void task_create(void (*entry)(void))
{
    task_create_named(entry, "unnamed");
}

extern void kprint(const char *);
extern void kprint_uint(uint32_t);

void task_list(void)
{
    for (int i = 0; i < task_count; i++) {
        if (!tasks[i].used) continue;
        kprint("  [");
        kprint_uint((uint32_t)i);
        kprint("] ");
        kprint(tasks[i].name);
        if (i == current_task) kprint(" - running");
        kprint("\n");
    }
}

int task_kill(int id)
{
    if (id < 0 || id >= task_count) return -1;
    if (!tasks[id].used) return -1;
    if (id == current_task) return -1; /* can't tear down our own running stack */
    if (id == 0) return -1; /* kernel */

    kfree(tasks[id].stack_base);
    tasks[id].used = 0;
    return 0;
}

int task_current_id(void)
{
    return current_task;
}