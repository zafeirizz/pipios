/* =========================================================================
 * AuroraOS - core kernel definitions
 * ========================================================================= */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ---------------- boot info (filled by boot/stage2.asm) ---------------- */
#define BOOT_INFO_MAGIC 0x42534F41u   /* 'AOSB' */

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;          /* 1 = usable RAM */
    uint32_t acpi;
} __attribute__((packed)) e820_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t lfb;
    uint32_t e820_count;
    uint32_t _pad;
    uint64_t e820_ptr;
    uint32_t kernel_size;
} __attribute__((packed)) boot_info_t;

/* ---------------- serial port (COM1) ---------------- */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

/* ---------------- printf ---------------- */
int ksnprintf(char *buf, size_t n, const char *fmt, ...);
int kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
void kprintf(const char *fmt, ...);
void kprintf_(const char *fmt, va_list ap);
void panic(const char *fmt, ...) __attribute__((noreturn));

/* ---------------- string / memory ---------------- */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);
char *strcat(char *dst, const char *src);
long strtol(const char *s, char **end, int base);
int atoi_(const char *s);

/* ---------------- ATA + FAT32 ---------------- */
int      ata_init(void);
int      ata_read(uint32_t lba, uint16_t count, void *buf);
int      ata_write(uint32_t lba, uint16_t count, const void *buf);
int      ata_write_nb(uint32_t lba, uint16_t count, const void *buf);
int      ata_flush(void);
int      ata_present(void);

typedef struct {
    int      used, is_dir;
    uint32_t first, cur, cur_idx, size, pos;
    uint32_t de_idx, de_dir;
    char     name[128];
} fs_file_t;

typedef struct { uint32_t dir_first, idx; } fs_dir_t;

typedef struct {
    char     name[128];
    uint32_t size, first;
    uint8_t  is_dir;
} fs_dirent_t;

int      fs_mount(void);
int      fs_mounted(void);
int      fs_open(const char *path, fs_file_t *f);
int      fs_create(const char *path, fs_file_t *f);
int      fs_read(fs_file_t *f, void *buf, uint32_t n);
int      fs_write(fs_file_t *f, const void *buf, uint32_t n);
void     fs_close(fs_file_t *f);
int      fs_opendir(const char *path, fs_dir_t *d);
int      fs_readdir(fs_dir_t *d, fs_dirent_t *out);
int      fs_unlink(const char *path);
int      fs_mkdir(const char *path);
uint32_t fs_free_bytes(void);

/* ---------------- PS/2 input ---------------- */
void     kbd_init(void);
int      kbd_getchar(void);        /* blocking ASCII read            */
int      kbd_try_getchar(void);    /* -1 if empty                    */
int      kbd_try_scancode(void);   /* raw set1 code, -1 if empty     */
void     mouse_init(void);
int32_t  mouse_x(void);
int32_t  mouse_y(void);
uint8_t  mouse_buttons(void);
uint32_t mouse_events(void);
uint32_t mouse_raw_bytes(void);
void     mouse_warp(int32_t x, int32_t y);

/* ---------------- physical memory manager ---------------- */
#define PAGE_SIZE 4096ULL
void     pmm_init(boot_info_t *bi);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint64_t count);
void     pmm_free_page(uint64_t phys);
void     pmm_free_pages(uint64_t phys, uint64_t count);
uint64_t pmm_free_pages_count(void);
uint64_t pmm_total_pages(void);

/* ---------------- kernel heap ---------------- */
void  heap_init(uint64_t base, size_t size);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void *krealloc(void *p, size_t n);
void  kfree(void *p);

/* ---------------- GDT / IDT / PIC / PIT ---------------- */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) regs_t;

void gdt_init(void);
void idt_init(void);
void pic_init(void);
void pic_set_mask(int irq, int masked);
void pic_send_eoi(int irq);
typedef void (*irq_handler_t)(regs_t *r);
void irq_register(int irq, irq_handler_t h);
void pit_init(uint32_t hz);
uint64_t timer_ticks(void);
uint64_t timer_ms(void);
void sleep_ms(uint32_t ms);

/* ---------------- interrupts ---------------- */
static inline void irq_enable(void)  { __asm__ volatile ("sti"); }
static inline void irq_disable(void) { __asm__ volatile ("cli"); }
static inline void cpu_halt(void)    { __asm__ volatile ("hlt"); }
static inline uint64_t read_cr2(void) {
    uint64_t v; __asm__ volatile ("mov %%cr2, %0" : "=r"(v)); return v;
}

/* ---------------- port I/O ---------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_relax(void) { __asm__ volatile ("pause"); }
