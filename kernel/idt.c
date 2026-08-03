#include <stdint.h>


extern void keyboard_stub();
extern void timer_stub();

struct idt_entry
{
    uint16_t base_low;
    uint16_t selector;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;

} __attribute__((packed));


struct idt_ptr
{
    uint16_t limit;
    uint32_t base;

} __attribute__((packed));


struct idt_entry idt[256];
struct idt_ptr idtp;


static void idt_set_gate(uint8_t num, uint32_t base, uint8_t present) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].selector = 0x08;
    idt[num].always0 = 0;

    idt[num].flags = (present << 7) | 0x8E; // Set the present bit based on the parameter
}

void idt_init()
{

    idtp.limit = sizeof(idt)-1;
    idtp.base = (uint32_t)&idt;


    for(int i=0;i<256;i++)
    {
        idt_set_gate(i, 0, 0);
    }


    idt_set_gate(33,(uint32_t)keyboard_stub, 1);
    idt_set_gate(32, (uint32_t)timer_stub, 1);

    asm volatile("lidt %0" : : "m"(idtp));
}