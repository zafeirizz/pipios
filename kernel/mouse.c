/* ==========================================================================
 * AuroraOS - PS/2 mouse driver (IRQ12, 3-byte standard packets)
 * ========================================================================== */
#include "include/aurora.h"

#define PSM_DATA    0x60
#define PSM_STATUS  0x64

static volatile int32_t  mx = 512, my = 384;
static volatile uint8_t  mbuttons;
static volatile uint32_t mevents;          /* bumped per packet (GUI redraw) */
static volatile uint32_t mraw;             /* bytes seen by the IRQ handler  */
static uint8_t  pkt[3];
static int      pidx;

static void psm_wait(uint8_t mask, uint8_t want) {
    for (int i = 0; i < 100000; i++)
        if ((inb(PSM_STATUS) & mask) == want) return;
}

static void psm_write(uint8_t b) {
    psm_wait(2, 0);            /* input buffer empty */
    outb(PSM_STATUS, 0xD4);
    psm_wait(2, 0);
    outb(PSM_DATA, b);
}

/* Bounded read: returns 0xFF if nothing shows up (avoids init hangs). */
static uint8_t psm_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(PSM_STATUS) & 1) return inb(PSM_DATA);
    return 0xFF;
}

/* Drop any stale bytes sitting in the controller output buffer. */
static void psm_drain(void) {
    while (inb(PSM_STATUS) & 1) inb(PSM_DATA);
}

/* Send a mouse command and wait specifically for its 0xFA ACK. */
static uint8_t psm_command(uint8_t cmd) {
    psm_drain();
    psm_write(cmd);
    for (int i = 0; i < 100000; i++) {
        if (inb(PSM_STATUS) & 1) {
            uint8_t b = inb(PSM_DATA);
            if (b == 0xFA) return 0xFA;
        }
    }
    return 0xFF;
}

static void mouse_irq(regs_t *r) {
    (void)r;
    uint8_t b = inb(PSM_DATA);
    mraw++;

    /* Byte 0 of a packet is 0bxx001xxx: bit3 set, overflow bits clear.
     * Anything else at packet start is controller noise (e.g. a late ACK
     * flushed out of the i8042 after IRQ12 is unmasked) -- drop it. */
    if (pidx == 0 && (b & 0xC8) != 0x08) return;
    pkt[pidx++] = b;
    if (pidx < 3) return;
    pidx = 0;

    if (pkt[0] & 0xC0) return;             /* X/Y overflow: drop */

    int32_t dx = (int8_t)pkt[1];
    int32_t dy = -(int8_t)pkt[2];          /* PS/2 y is screen-up */
    uint8_t nb = pkt[0] & 0x07;

    mx += dx;
    my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (nb != mbuttons) {
        mbuttons = nb;
        kprintf("[mouse] %d,%d buttons=%u\n", (int)mx, (int)my, nb);
    }
    mevents++;
}

void mouse_init(void) {
    pidx = 0; mx = 512; my = 384; mbuttons = 0; mevents = 0; mraw = 0;

    psm_drain();
    psm_wait(2, 0);
    outb(PSM_STATUS, 0xA8);               /* enable auxiliary port */

    psm_drain();
    psm_wait(2, 0);
    outb(PSM_STATUS, 0x20);               /* read controller config */
    uint8_t cfg = psm_read();
    cfg |=  0x03;                         /* IRQ1 (kbd) + IRQ12 (mouse) */
    cfg &= (uint8_t)~0x30;                /* un-disable both interfaces */
    psm_wait(2, 0);
    outb(PSM_STATUS, 0x60);               /* write controller config */
    psm_wait(2, 0);
    outb(PSM_DATA, cfg);

    uint8_t ack1 = psm_command(0xF6);     /* set defaults     */
    uint8_t ack2 = psm_command(0xF4);     /* enable streaming */
    psm_drain();                          /* nothing stale when IRQ12 opens */

    irq_register(12, mouse_irq);
    kprintf("[mouse] cfg=%02x ack=%02x/%02x\n", cfg, ack1, ack2);
}

uint32_t mouse_raw_bytes(void) { return mraw; }
int32_t  mouse_x(void)        { return mx; }
int32_t  mouse_y(void)        { return my; }
uint8_t  mouse_buttons(void)  { return mbuttons; }
uint32_t mouse_events(void)   { return mevents; }

/* GUI moves the cursor directly; clamp handled by caller via this setter */
void mouse_warp(int32_t x, int32_t y) { mx = x; my = y; }
