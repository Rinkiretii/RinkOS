#include <stdint.h>
#include "scr/io.h"


extern void kprint(const char*);


void keyboard_handler()
{
    uint8_t scancode;

    scancode = inb(0x60);


    if(scancode == 0x1C)
    {
        kprint("ENTER\n");
    }


    // сообщаем PIC, что закончили
    outb(0x20,0x20);
}