/* AuroraOS - GDT + TSS setup for long mode */
#include "include/aurora.h"

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_tss_entry {          /* 16-byte system descriptor */
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
    uint32_t base_top;
    uint32_t zero;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[7];
static struct tss tss;
static struct gdtr gdtr;

static void set_code(int idx, uint8_t access, uint8_t gran) {
    gdt[idx].limit_lo = 0xFFFF;
    gdt[idx].base_lo  = 0;
    gdt[idx].base_mid = 0;
    gdt[idx].access    = access;
    gdt[idx].gran      = gran;
    gdt[idx].base_hi  = 0;
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    /* 0x00 null */
    /* 0x08 kernel code (64-bit) */
    set_code(1, 0x9A, 0xA0);        /* present, ring0, code, L=1 */
    /* 0x10 kernel data */
    set_code(2, 0x92, 0xC0);        /* present, ring0, data, DB=1 */

    /* 0x18 TSS (16-byte descriptor covering entries 3 and 4) */
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    struct gdt_tss_entry *te = (struct gdt_tss_entry *)&gdt[3];
    te->limit_lo = tss_limit & 0xFFFF;
    te->base_lo  = tss_base & 0xFFFF;
    te->base_mid = (tss_base >> 16) & 0xFF;
    te->access   = 0x89;            /* present, type = available TSS */
    te->gran     = (tss_limit >> 16) & 0x0F;
    te->base_hi  = (tss_base >> 24) & 0xFF;
    te->base_top = (uint32_t)(tss_base >> 32);
    te->zero     = 0;

    tss.iomap_base = sizeof(tss);
    tss.rsp0 = 0;                   /* set later by scheduler/GUI */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;
    __asm__ volatile ("lgdt %0" : : "m"(gdtr));

    /* reload data segments */
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :: : "rax", "memory"
    );

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x18));
}

void tss_set_rsp0(uint64_t rsp) { tss.rsp0 = rsp; }
