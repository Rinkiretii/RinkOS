#include <stdint.h>
#include "scr/io.h"
#include "scr/timer.h"

static volatile uint32_t ticks = 0;
static uint32_t tick_frequency = 100; /* set by timer_init */

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182u

void timer_init(uint32_t frequency_hz)
{
    tick_frequency = frequency_hz;
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / frequency_hz);

    outb(PIT_COMMAND, 0x36);              /* channel 0, lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void timer_handler(void)
{
    ticks++;
    outb(0x20, 0x20);   /* EOI, same as keyboard_handler */
}

uint32_t timer_get_ticks(void)
{
    return ticks;
}

uint32_t timer_get_seconds(void)
{
    return ticks / tick_frequency;
}