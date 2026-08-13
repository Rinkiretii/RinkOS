/* ============================================================
 * RinkOS - Base kernel
 * Freestanding C kernel. No standard library available: the
 * bootloader dropped us here in 32-bit protected mode with a
 * flat memory model and a working stack. From here on, we own
 * the machine.
 * ============================================================ */

#include <stdint.h>
#include "scr/keyboard.h"
#include "scr/io.h"
#include "scr/mm.h"
#include "scr/mmap.h"
#include "scr/shell.h"
#include "scr/timer.h"
#include "scr/task.h"
#include "scr/shell.h"
#include "scr/disk.h"
#include "scr/fs.h"
 
/* VGA text mode lives at a fixed physical address once the
 * BIOS has set it up; 80x25 characters, 2 bytes per cell
 * (ASCII byte + color attribute byte). */
#define VGA_ADDRESS   0xB8000
#define VGA_WIDTH     80
#define VGA_HEIGHT    25
#define VGA_COLOR     0x0F   /* white text on black background */

static uint16_t *const vga_buffer = (uint16_t *) VGA_ADDRESS;
extern uint8_t inb(uint16_t port);
static int cursor_row = 0;
static int cursor_col = 0;

extern void idt_init();
extern void pic_remap();
extern void keyboard_debug_init(void);
extern void keyboard_handler(void);
extern void kmalloc_init();
extern void kfree();
extern void mmap_dump();
extern void shell_run();
extern void timer_init();
extern void tasks_init();
extern void fs_init();

char *system_version = "RinkOS 0.024";

void vga_update_cursor(void)
{
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)(' ' | (VGA_COLOR << 8));
    }
    cursor_row = 0;
    cursor_col = 0;
}

void vga_scroll_if_needed(void)
{
    if (cursor_row < VGA_HEIGHT) {
        return;
    }

    /* move every line up by one */
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[(row - 1) * VGA_WIDTH + col] = vga_buffer[row * VGA_WIDTH + col];
        }
    }
    /* clear the last line */
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = (uint16_t)(' ' | (VGA_COLOR << 8));
    }
    cursor_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
        const int index = cursor_row * VGA_WIDTH + cursor_col;
        vga_buffer[index] = (uint16_t)(' ' | (VGA_COLOR << 8));
    } else {
        const int index = cursor_row * VGA_WIDTH + cursor_col;
        vga_buffer[index] = (uint16_t)((uint8_t)c | (VGA_COLOR << 8));
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    vga_scroll_if_needed();
    vga_update_cursor();
} 

void kprint(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i]);
    }
}

void vga_enable_cursor(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0D);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

void reboot(void)
{
    asm volatile("cli");

    /* try the keyboard controller pulse first */
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    /* fallback: force a triple fault (hard reset) if that didn't work */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) bad_idt = {0, 0};

    asm volatile("lidt %0" : : "m"(bad_idt));
    asm volatile("int $0x03");  /* deliberately trigger an unhandled exception */

    asm volatile("hlt");
}

void shutdown(void) {
}

void kprint_hex32(uint32_t val)
{
    char buf[11];
    const char hex[] = "0123456789ABCDEF";

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(val >> ((7 - i) * 4)) & 0xF];
    }
    buf[10] = '\0';

    kprint(buf);
}

void shell_task(void) {
    asm volatile("sti");
    shell_run();
}

void kernel(void) {
    asm volatile("sti");

    task_create_named(shell_task, "shell");
    fs_init();

    while (inb(0x64) & 0x01) { inb(0x60); }  /* wait for keyboard controller to be ready */
    
    asm volatile("sti");  /* enable interrupts */

    for (;;) {
        asm volatile("hlt");
    }

    kmalloc_init();
}

/* Entry point called from kernel_entry.asm */
void kernel_main(void)
{
    vga_clear();
    kmalloc_init(); 

    kprint("RinkOS kernel loaded successfully.\n");
    kprint("Welcome to RinkOS!\n");
    kprint("--------------------------------\n");
    kprint("Kernel is running in 32-bit protected mode.\n");
    kprint("\n");
    kprint(system_version);
    kprint("\n");

    idt_init();
    pic_remap();
    tasks_init();
    task_create_named(kernel, "kernel");

    while (inb(0x64) & 0x01) { inb(0x60); }  /* wait for keyboard controller to be ready */
    
    asm volatile("sti");  /* enable interrupts */

    for (;;) {
        asm volatile("hlt");
    }
}