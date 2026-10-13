/* ==========================================================================
 * AuroraOS - kernel heap (first-fit free list with split + coalesce)
 *
 * The arena is carved out of physical memory by pmm_alloc_pages() and used
 * through kmalloc/kzalloc/krealloc/kfree. Block headers carry a 16-byte
 * aligned payload so SSE-free code and structs with 16-byte alignment
 * both work.
 * ========================================================================== */
#include "include/aurora.h"

#define HEAP_ALIGN      16ULL
#define HEAP_MIN_SPLIT  (sizeof(heap_block_t) + HEAP_ALIGN)

typedef struct heap_block {
    size_t size;                 /* payload bytes, multiple of HEAP_ALIGN */
    struct heap_block *next;     /* address-ordered list                  */
    int free;
} __attribute__((aligned(HEAP_ALIGN))) heap_block_t;

static heap_block_t *heap_head;
static uint8_t *heap_start, *heap_end;

static inline size_t align_up(size_t n) {
    return (n + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);
}

void heap_init(uint64_t base, size_t size) {
    heap_start = (uint8_t *)base;
    heap_end   = heap_start + size;
    heap_head  = (heap_block_t *)heap_start;
    heap_head->size = size - sizeof(heap_block_t);
    heap_head->next = NULL;
    heap_head->free = 1;
}

/* Split blk so it keeps `need` payload bytes; remainder becomes a block. */
static void split_block(heap_block_t *blk, size_t need) {
    if (blk->size < need + HEAP_MIN_SPLIT) return;
    heap_block_t *rest = (heap_block_t *)((uint8_t *)blk + sizeof(heap_block_t) + need);
    rest->size = blk->size - need - sizeof(heap_block_t);
    rest->next = blk->next;
    rest->free = blk->free;
    blk->size  = need;
    blk->next  = rest;
}

void *kmalloc(size_t n) {
    if (n == 0) return NULL;
    n = align_up(n);
    for (heap_block_t *b = heap_head; b; b = b->next) {
        if (b->free && b->size >= n) {
            split_block(b, n);
            b->free = 0;
            return (uint8_t *)b + sizeof(heap_block_t);
        }
    }
    return NULL;
}

void *kzalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    heap_block_t *b = (heap_block_t *)((uint8_t *)p - sizeof(heap_block_t));
    if ((uint8_t *)b < heap_start || (uint8_t *)b >= heap_end)
        panic("kfree: pointer outside heap (%p)", p);
    b->free = 1;

    /* forward coalesce */
    while (b->next && b->next->free) {
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next  = b->next->next;
    }
    /* backward coalesce */
    for (heap_block_t *prev = heap_head; prev; prev = prev->next) {
        if (prev->next == b && prev->free) {
            prev->size += sizeof(heap_block_t) + b->size;
            prev->next  = b->next;
            break;
        }
    }
}

void *krealloc(void *p, size_t n) {
    if (!p) return kmalloc(n);
    if (n == 0) { kfree(p); return NULL; }
    heap_block_t *b = (heap_block_t *)((uint8_t *)p - sizeof(heap_block_t));
    if (b->size >= n) return p;

    void *q = kmalloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    kfree(p);
    return q;
}
