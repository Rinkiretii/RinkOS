#include <stdint.h>
#include "scr/io.h"
#define KEYBOARD_BUFFER_SIZE 128

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int buffer_read = 0;
static int buffer_write = 0;

extern void kprint(const char*);

/* ---- temporary debug serial logging (COM1) ---- */
#define COM1 0x3F8

void keyboard_debug_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void dbg_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}

static void dbg_str(const char *s) { while (*s) dbg_putc(*s++); }

static void dbg_hex(uint8_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    dbg_putc(hex[(val >> 4) & 0xF]);
    dbg_putc(hex[val & 0xF]);
}
/* ---- end debug ---- */

static const char keyboard_map[128] =
{
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/'
};

void keyboard_push(char c)
{
    int next = (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;

    dbg_str("PUSH c="); dbg_hex((uint8_t)c);
    dbg_str(" w="); dbg_hex((uint8_t)buffer_write);
    dbg_str(" r="); dbg_hex((uint8_t)buffer_read);
    dbg_putc('\n');

    if (next != buffer_read) {
        keyboard_buffer[buffer_write] = c;
        buffer_write = next;
    } else {
        dbg_str("PUSH DROPPED (full)\n");
    }
}

char keyboard_getchar(void)
{
    while (buffer_read == buffer_write) {
        asm volatile("hlt");
    }
    char c = keyboard_buffer[buffer_read];
    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;

    dbg_str("GET c="); dbg_hex((uint8_t)c); dbg_putc('\n');
    return c;
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);

    dbg_str("IRQ sc="); dbg_hex(scancode); dbg_putc('\n');

    if (scancode < 128) {
        char c = keyboard_map[scancode];
        if (c != 0) {
            keyboard_push(c);
        }
    }
    outb(0x20, 0x20);
}