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

static void vga_update_cursor(void)
{
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)(' ' | (VGA_COLOR << 8));
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void vga_scroll_if_needed(void)
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

/* Entry point called from kernel_entry.asm */
void kernel_main(void)
{
    vga_clear();
    kprint("RinkOS kernel loaded successfully.\n");
    kprint("Welcome to RinkOS!\n");
    kprint("--------------------------------\n");
    kprint("Kernel is running in 32-bit protected mode.\n");
    kprint("\n");
    
    idt_init();
    pic_remap();

    asm volatile("sti");
    
    while (1) {
        char c = keyboard_getchar();
        char str[2] = {c, '\0'};
        kprint(str);
    }
}