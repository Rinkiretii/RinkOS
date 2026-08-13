#include <stdint.h>
#include "scr/io.h"
#define KEYBOARD_BUFFER_SIZE 128

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int buffer_read = 0;
static int buffer_write = 0;

static int shift_pressed = 0;
static int capslock_on = 0;

extern void kprint(const char*);

#define SC_LSHIFT_DOWN 0x2A
#define SC_RSHIFT_DOWN 0x36
#define SC_LSHIFT_UP   0xAA
#define SC_RSHIFT_UP   0xB6
#define SC_CAPSLOCK    0x3A

static const char keyboard_map[128] =
{
    /* 0x00 */ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    /* 0x10 */ 'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    /* 0x1E */ 'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    /* 0x2C */ 'z','x','c','v','b','n','m',',','.','/',0,'*',0,
    /* 0x39 */ ' '   /* space bar */
};

static const char keyboard_map_shift[128] =
{
    /* 0x00 */ 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    /* 0x10 */ 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    /* 0x1E */ 'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    /* 0x2C */ 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,
    /* 0x39 */ ' '
};

static int is_letter(char c)
{
    return (c >= 'a' && c <= 'z');
}

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
 
    if (scancode == SC_LSHIFT_DOWN || scancode == SC_RSHIFT_DOWN) {
        shift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == SC_LSHIFT_UP || scancode == SC_RSHIFT_UP) {
        shift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }
 
    if (scancode == SC_CAPSLOCK) {
        capslock_on = !capslock_on;
        outb(0x20, 0x20);
        return;
    }

    if (scancode & 0x80) {
        outb(0x20, 0x20);
        return;
    }
 
    if (scancode < 128) {
        char c;
 
        if (shift_pressed) {
            c = keyboard_map_shift[scancode];
        } else {
            c = keyboard_map[scancode];
        }
 
        if (capslock_on && is_letter(keyboard_map[scancode])) {
            if (shift_pressed) {
                c = keyboard_map[scancode];
            } else {
                c = keyboard_map_shift[scancode];
            }
        }
 
        if (c != 0) {
            keyboard_push(c);
        }
    }
    outb(0x20, 0x20);
}