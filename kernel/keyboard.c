#include <stdint.h>
#include "scr/io.h"
#define KEYBOARD_BUFFER_SIZE 128

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int buffer_read = 0;
static int buffer_write = 0;

extern void kprint(const char*);

static const char keyboard_map[128] =
{
    /* 0x00 */ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    /* 0x10 */ 'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    /* 0x1E */ 'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    /* 0x2C */ 'z','x','c','v','b','n','m',',','.','/',0,'*',0,
    /* 0x39 */ ' '   /* space bar */
};

void keyboard_push(char c)
{
    int next = (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buffer_read) {
        keyboard_buffer[buffer_write] = c;
        buffer_write = next;
    }
}

char keyboard_getchar(void)
{
    while (buffer_read == buffer_write) {
        asm volatile("hlt");
    }
    char c = keyboard_buffer[buffer_read];
    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);

    if (scancode < 128) {
        char c = keyboard_map[scancode];
        if (c != 0) {
            keyboard_push(c);
        }
    }
    outb(0x20, 0x20);
}