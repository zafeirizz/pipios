/* AuroraOS - IDT setup, exception handling, IRQ dispatch */
#include "include/aurora.h"

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idtr idtr;

extern void *isr_stub_table[48];

static const char *exception_names[32] = {
    "Divide Error (#DE)", "Debug (#DB)", "NMI", "Breakpoint (#BP)",
    "Overflow (#OF)", "Bound Range (#BR)", "Invalid Opcode (#UD)",
    "Device Not Available (#NM)", "Double Fault (#DF)", "Coproc Segment Overrun",
    "Invalid TSS (#TS)", "Segment Not Present (#NP)", "Stack Fault (#SS)",
    "General Protection (#GP)", "Page Fault (#PF)", "Reserved",
    "x87 FP (#MF)", "Alignment Check (#AC)", "Machine Check (#MC)",
    "SIMD FP (#XM)", "Virtualization (#VE)", "Control Protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security (#SX)", "Reserved"
};

static void idt_set(int vec, void *handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_lo = addr & 0xFFFF;
    idt[vec].selector  = 0x08;
    idt[vec].ist       = 0;
    idt[vec].type_attr = type_attr;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_hi  = (uint32_t)(addr >> 32);
    idt[vec].zero      = 0;
}

void idt_init(void) {
    memset(&idt, 0, sizeof(idt));
    for (int i = 0; i < 48; i++)
        idt_set(i, isr_stub_table[i], 0x8E);    /* present, ring0, interrupt gate */
    /* leave the rest empty (would triple-fault) */

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

/* ---------------- IRQ handler registration ---------------- */
static irq_handler_t irq_handlers[16];

void irq_register(int irq, irq_handler_t h) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = h;
        pic_set_mask(irq, h == 0);
    }
}

static void dump_frame(regs_t *r) {
    kprintf("  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n",
            (unsigned long long)r->rax, (unsigned long long)r->rbx,
            (unsigned long long)r->rcx, (unsigned long long)r->rdx);
    kprintf("  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n",
            (unsigned long long)r->rsi, (unsigned long long)r->rdi,
            (unsigned long long)r->rbp, (unsigned long long)r->rsp);
    kprintf("  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n",
            (unsigned long long)r->r8, (unsigned long long)r->r9,
            (unsigned long long)r->r10, (unsigned long long)r->r11);
    kprintf("  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
            (unsigned long long)r->r12, (unsigned long long)r->r13,
            (unsigned long long)r->r14, (unsigned long long)r->r15);
    kprintf("  rip=%016llx cs=%04llx rflags=%016llx\n",
            (unsigned long long)r->rip, (unsigned long long)r->cs,
            (unsigned long long)r->rflags);
}

void isr_dispatch(regs_t *r) {
    uint64_t vec = r->vector;
    if (vec < 32) {
        irq_disable();
        if (vec == 14) {
            kprintf("\n*** PAGE FAULT: access to %016llx (cr2) at rip %016llx, error=%llx\n",
                    (unsigned long long)read_cr2(),
                    (unsigned long long)r->rip, (unsigned long long)r->error);
        } else {
            kprintf("\n*** EXCEPTION %llu (%s), error=%llx\n",
                    (unsigned long long)vec, exception_names[vec],
                    (unsigned long long)r->error);
        }
        dump_frame(r);
        panic("unhandled CPU exception");
    }
    if (vec >= 32 && vec < 48) {
        int irq = (int)vec - 32;
        irq_handler_t h = irq_handlers[irq];
        pic_send_eoi(irq);
        if (h) h(r);
        return;
    }
    /* spurious/unknown */
    if (vec < 256 && irq_handlers[0]) {
        kprintf("unhandled interrupt %llu\n", (unsigned long long)vec);
    }
}
