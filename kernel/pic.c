/* AuroraOS - 8259A PIC driver */
#include "include/aurora.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static uint16_t irq_mask = 0xFFFF;

void pic_init(void) {
    outb(PIC1_CMD, 0x11); io_wait();     /* ICW1: init + ICW4 */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();    /* ICW2: vector offsets 32..39 */
    outb(PIC2_DATA, 0x28); io_wait();    /*         40..47 */
    outb(PIC1_DATA, 0x04); io_wait();    /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();    /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);               /* mask everything initially */
    outb(PIC2_DATA, 0xFF);
    irq_mask = 0xFFFF;
}

void pic_set_mask(int irq, int masked) {
    if (irq < 0 || irq > 15) return;
    if (masked) irq_mask |= (uint16_t)(1 << irq);
    else        irq_mask &= (uint16_t)~(1 << irq);
    if (irq >= 8) irq_mask &= (uint16_t)~(1 << 2);   /* keep cascade unmasked */
    outb(PIC1_DATA, (uint8_t)(irq_mask & 0xFF));
    outb(PIC2_DATA, (uint8_t)(irq_mask >> 8));
}

void pic_send_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
