/* AuroraOS - 8254 PIT timer */
#include "include/aurora.h"

static volatile uint64_t g_ticks = 0;
static uint32_t g_hz = 100;

static void timer_irq(regs_t *r) {
    (void)r;
    g_ticks++;
}

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    g_hz = hz;
    uint32_t divisor = 1193182 / hz;
    outb(0x43, 0x36);                   /* channel 0, lo/hi byte, mode 3 */
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    irq_register(0, timer_irq);
}

uint64_t timer_ticks(void) { return g_ticks; }
uint64_t timer_ms(void)    { return g_ticks * (1000 / g_hz); }

void sleep_ms(uint32_t ms) {
    uint64_t target = g_ticks + (ms * g_hz + 999) / 1000;
    while (g_ticks < target) cpu_halt();
}
