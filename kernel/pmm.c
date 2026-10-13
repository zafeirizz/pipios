/* ==========================================================================
 * AuroraOS - physical memory manager (page-frame bitmap allocator)
 *
 * Tracks up to PMM_MAX_MEM bytes of RAM using a 1-bit-per-page bitmap.
 * 1 bit = one 4 KiB frame; 1 = free, 0 = reserved/used. The bitmap itself
 * lives in .bss and is therefore reserved together with the kernel image.
 * ========================================================================== */
#include "include/aurora.h"

#define PMM_PAGE_SIZE   4096ULL
#define PMM_MAX_MEM     (256ULL << 20)              /* track up to 256 MiB */
#define PMM_MAX_PAGES   (PMM_MAX_MEM / PMM_PAGE_SIZE)
#define PMM_BITMAP_BITS PMM_MAX_PAGES
#define PMM_BITMAP_SIZE (PMM_BITMAP_BITS / 8)       /* 8 KiB */

static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];
static uint64_t pmm_total;    /* tracked frames   */
static uint64_t pmm_free;     /* free frames      */
static uint64_t pmm_hint;     /* next-fit cursor  */

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

static inline void bitmap_set(uint64_t p)   { pmm_bitmap[p >> 3] |=  (uint8_t)(1u << (p & 7)); }
static inline void bitmap_clr(uint64_t p)   { pmm_bitmap[p >> 3] &= (uint8_t)~(1u << (p & 7)); }
static inline int  bitmap_test(uint64_t p)  { return pmm_bitmap[p >> 3] & (1u << (p & 7)); }

/* Mark [base, base+len) free (clamped to the tracked window). */
static void pmm_free_range(uint64_t base, uint64_t len) {
    if (len == 0) return;
    if (base >= PMM_MAX_MEM) return;
    if (base + len > PMM_MAX_MEM) len = PMM_MAX_MEM - base;

    uint64_t first = (base + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;  /* ceil */
    uint64_t last  = (base + len) / PMM_PAGE_SIZE;                /* excl */
    if (last > pmm_total) last = pmm_total;
    for (uint64_t p = first; p < last; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            pmm_free++;
        }
    }
}

/* Mark [base, base+len) reserved (clamped to the tracked window). */
static void pmm_reserve_range(uint64_t base, uint64_t len) {
    if (base >= PMM_MAX_MEM) return;
    uint64_t first = base / PMM_PAGE_SIZE;                          /* floor */
    uint64_t last  = (base + len + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (last > pmm_total) last = pmm_total;
    for (uint64_t p = first; p < last; p++) {
        if (bitmap_test(p)) {
            bitmap_clr(p);
            pmm_free--;
        }
    }
}

void pmm_init(boot_info_t *bi) {
    memset(pmm_bitmap, 0, sizeof(pmm_bitmap));

    /* Highest usable address (capped) determines how much we track. */
    e820_entry_t *e820 = (e820_entry_t *)bi->e820_ptr;
    uint64_t top = 0;
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        if (e820[i].type != 1) continue;
        uint64_t end = e820[i].base + e820[i].length;
        if (end > top) top = end;
    }
    if (top > PMM_MAX_MEM) top = PMM_MAX_MEM;
    pmm_total = top / PMM_PAGE_SIZE;
    pmm_free  = 0;

    /* Free usable RAM first, then reserve the carve-outs. */
    for (uint32_t i = 0; i < bi->e820_count; i++)
        if (e820[i].type == 1)
            pmm_free_range(e820[i].base, e820[i].length);

    pmm_reserve_range(0, 0x100000);                            /* low 1 MiB: IVT, BIOS, boot structs, page tables */
    pmm_reserve_range((uint64_t)__kernel_start,
                      (uint64_t)(__kernel_end - __kernel_start)); /* kernel image incl. .bss */

    pmm_hint = 0x100;  /* skip past the kernel for speed */
    kprintf("pmm: %llu frames tracked, %llu MiB free\n",
            (unsigned long long)pmm_total, (unsigned long long)(pmm_free << 12 >> 20));
}

uint64_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

uint64_t pmm_alloc_pages(uint64_t count) {
    if (count == 0 || pmm_free < count) return 0;

    uint64_t run = 0, start = 0;
    uint64_t begin = pmm_hint < pmm_total ? pmm_hint : 0;

    for (uint64_t i = begin; i < pmm_total && run < count; i++) {
        if (bitmap_test(i)) {
            if (run == 0) start = i;
            run++;
        } else {
            run = 0;
        }
    }
    /* wrap-around scan for the region before the hint */
    for (uint64_t i = 0; i < begin && run < count; i++) {
        if (bitmap_test(i)) {
            if (run == 0) start = i;
            run++;
        } else {
            run = 0;
        }
    }
    if (run < count) return 0;

    for (uint64_t p = start; p < start + count; p++) bitmap_clr(p);
    pmm_free -= count;
    pmm_hint = start + count;
    return start * PMM_PAGE_SIZE;
}

void pmm_free_pages(uint64_t phys, uint64_t count) {
    if (phys == 0 || (phys & (PMM_PAGE_SIZE - 1)) || count == 0) return;
    uint64_t start = phys / PMM_PAGE_SIZE;
    if (start + count > pmm_total) return;
    for (uint64_t p = start; p < start + count; p++) {
        if (bitmap_test(p)) {
            kprintf("pmm: double free at frame %llu\n", (unsigned long long)p);
            return;
        }
        bitmap_set(p);
    }
    pmm_free += count;
    if (start < pmm_hint) pmm_hint = start;
}

void pmm_free_page(uint64_t phys) { pmm_free_pages(phys, 1); }

uint64_t pmm_free_pages_count(void) { return pmm_free; }
uint64_t pmm_total_pages(void)      { return pmm_total; }
