/* AuroraOS - COM1 serial driver */
#include "include/aurora.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* disable interrupts */
    outb(COM1 + 3, 0x80);    /* DLAB on */
    outb(COM1 + 0, 0x01);    /* divisor = 1 (115200 baud) */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    /* 8N1 */
    outb(COM1 + 2, 0xC7);    /* FIFO on, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);    /* DTR, RTS, OUT2 */
}

static int serial_tx_empty(void) { return inb(COM1 + 5) & 0x20; }

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!serial_tx_empty()) cpu_relax();
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}
