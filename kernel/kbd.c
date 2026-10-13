/* ==========================================================================
 * AuroraOS - PS/2 keyboard driver (IRQ1, scancode set 1, US layout)
 *
 * Produces two outputs:
 *   - an ASCII ring buffer (for the terminal / console)
 *   - a raw make/break scancode ring (for GUI apps that want key events)
 * ========================================================================== */
#include "include/aurora.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

/* ASCII ring buffer --------------------------------------------------- */
#define CBUF_SIZE 256
static volatile uint32_t cbuf_head, cbuf_tail;
static uint8_t cbuf[CBUF_SIZE];

/* raw scancode ring --------------------------------------------------- */
#define SBUF_SIZE 64
static volatile uint32_t sbuf_head, sbuf_tail;
static uint8_t sbuf[SBUF_SIZE];

/* modifier state ------------------------------------------------------ */
static int shift_down, ctrl_down, caps_lock;

/* scancode set 1 -> ASCII (US), index = make code --------------------- */
static const char keymap_plain[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char keymap_shifted[128] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A

static void cbuf_push(uint8_t c) {
    uint32_t next = (cbuf_head + 1) & (CBUF_SIZE - 1);
    if (next == cbuf_tail) return;               /* full: drop */
    cbuf[cbuf_head] = c;
    cbuf_head = next;
}

static void sbuf_push(uint8_t c) {
    uint32_t next = (sbuf_head + 1) & (SBUF_SIZE - 1);
    if (next == sbuf_tail) return;
    sbuf[sbuf_head] = c;
    sbuf_head = next;
}

static void kbd_irq(regs_t *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA);
    sbuf_push(sc);

    if (sc & 0x80) {                               /* break code */
        uint8_t make = sc & 0x7F;
        if (make == SC_LSHIFT || make == SC_RSHIFT) shift_down = 0;
        if (make == SC_CTRL)                        ctrl_down  = 0;
        return;
    }

    switch (sc) {
    case SC_LSHIFT: case SC_RSHIFT: shift_down = 1; return;
    case SC_CTRL:                   ctrl_down  = 1; return;
    case SC_CAPS:                   caps_lock ^= 1; return;
    }

    if (sc >= 128) return;
    char ch = shift_down ? keymap_shifted[sc] : keymap_plain[sc];
    if (!ch) return;

    /* caps lock flips case for letters only; shift wins over caps */
    if (caps_lock && !ctrl_down) {
        if (ch >= 'a' && ch <= 'z')      ch -= 32;
        else if (ch >= 'A' && ch <= 'Z') ch += 32;
    }
    if (ctrl_down && ch >= 'a' && ch <= 'z') ch -= 'a' - 1;   /* Ctrl-A = 1 ... */
    cbuf_push((uint8_t)ch);
}

void kbd_init(void) {
    cbuf_head = cbuf_tail = 0;
    sbuf_head = sbuf_tail = 0;
    shift_down = ctrl_down = caps_lock = 0;

    /* drain any stale bytes */
    while (inb(KBD_STATUS) & 1) inb(KBD_DATA);

    irq_register(1, kbd_irq);
}

/* Blocking read; hlt's until a character arrives. */
int kbd_getchar(void) {
    for (;;) {
        irq_disable();
        if (cbuf_tail != cbuf_head) {
            uint8_t c = cbuf[cbuf_tail];
            cbuf_tail = (cbuf_tail + 1) & (CBUF_SIZE - 1);
            irq_enable();
            return c;
        }
        irq_enable();
        cpu_halt();
    }
}

/* Non-blocking read; -1 if no character pending. */
int kbd_try_getchar(void) {
    irq_disable();
    int ret = -1;
    if (cbuf_tail != cbuf_head) {
        ret = cbuf[cbuf_tail];
        cbuf_tail = (cbuf_tail + 1) & (CBUF_SIZE - 1);
    }
    irq_enable();
    return ret;
}

/* Non-blocking raw scancode (set 1 make/break); -1 if none pending. */
int kbd_try_scancode(void) {
    irq_disable();
    int ret = -1;
    if (sbuf_tail != sbuf_head) {
        ret = sbuf[sbuf_tail];
        sbuf_tail = (sbuf_tail + 1) & (SBUF_SIZE - 1);
    }
    irq_enable();
    return ret;
}
