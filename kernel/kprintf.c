/* AuroraOS - formatted output (kprintf family) and panic */
#include "include/aurora.h"

/* flags */
#define F_LEFT  1
#define F_ZERO  2
#define F_PLUS  4
#define F_HASH  8

static void out_char(char *buf, size_t cap, size_t *pos, char c) {
    if (buf && *pos + 1 < cap) buf[*pos] = c;
    (*pos)++;
}

static void out_str(char *buf, size_t cap, size_t *pos, const char *s, int width, int left) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    if (!left) for (int i = len; i < width; i++) out_char(buf, cap, pos, ' ');
    for (int i = 0; i < len; i++) out_char(buf, cap, pos, s[i]);
    if (left) for (int i = len; i < width; i++) out_char(buf, cap, pos, ' ');
}

static void out_num(char *buf, size_t cap, size_t *pos, uint64_t v, int base,
                    int is_signed, int width, int flags) {
    char tmp[24];
    const char *digits = "0123456789abcdef";
    const char *DIGITS = "0123456789ABCDEF";
    int n = 0, neg = 0;

    if (is_signed && (int64_t)v < 0) { neg = 1; v = (uint64_t)(-(int64_t)v); }
    if (v == 0) tmp[n++] = '0';
    while (v) {
        uint32_t d = (uint32_t)(v % (uint64_t)base);
        tmp[n++] = (base == 16 && flags & F_HASH) ? digits[d] : digits[d];
        if (base == 16 && (flags & F_HASH)) { /* prefix added later */ }
        (void)DIGITS;
        v /= (uint64_t)base;
    }
    /* reverse */
    for (int i = 0; i < n / 2; i++) { char t = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = t; }

    int total = n + neg;
    int pad = width > total ? width - total : 0;

    if (!(flags & F_LEFT) && !(flags & F_ZERO)) {
        if (neg) { out_char(buf, cap, pos, '-'); neg = 0; width--; pad--; }
        for (int i = 0; i < pad; i++) out_char(buf, cap, pos, ' ');
    } else if (!(flags & F_LEFT) && (flags & F_ZERO)) {
        if (neg) { out_char(buf, cap, pos, '-'); neg = 0; width--; pad--; }
        for (int i = 0; i < pad; i++) out_char(buf, cap, pos, '0');
    }
    if (neg) out_char(buf, cap, pos, '-');
    for (int i = 0; i < n; i++) out_char(buf, cap, pos, tmp[i]);
    if (flags & F_LEFT) for (int i = 0; i < pad; i++) out_char(buf, cap, pos, ' ');
}

int kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_char(buf, n, &pos, *fmt); continue; }
        fmt++;
        if (*fmt == '%') { out_char(buf, n, &pos, '%'); continue; }

        int flags = 0, width = 0, longs = 0;
        for (;; fmt++) {
            if (*fmt == '-') flags |= F_LEFT;
            else if (*fmt == '0') flags |= F_ZERO;
            else if (*fmt == '+') flags |= F_PLUS;
            else if (*fmt == '#') flags |= F_HASH;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        while (*fmt == 'l') { longs++; fmt++; }
        if (*fmt == 'z' || *fmt == 't') { longs = 1; fmt++; }
        if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!(flags & F_LEFT)) for (int i = 1; i < width; i++) out_char(buf, n, &pos, ' ');
            out_char(buf, n, &pos, c);
            if (flags & F_LEFT) for (int i = 1; i < width; i++) out_char(buf, n, &pos, ' ');
            break;
        }
        case 's':
            out_str(buf, n, &pos, va_arg(ap, const char *), width, flags & F_LEFT);
            break;
        case 'd': case 'i': {
            int64_t v = longs ? va_arg(ap, int64_t) : va_arg(ap, int);
            out_num(buf, n, &pos, (uint64_t)v, 10, 1, width, flags);
            break;
        }
        case 'u': {
            uint64_t v = longs ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int);
            out_num(buf, n, &pos, v, 10, 0, width, flags);
            break;
        }
        case 'x': case 'X': {
            uint64_t v = longs ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int);
            out_num(buf, n, &pos, v, 16, 0, width, flags);
            break;
        }
        case 'p':
            out_char(buf, n, &pos, '0');
            out_char(buf, n, &pos, 'x');
            out_num(buf, n, &pos, (uint64_t)va_arg(ap, void *), 16, 0, 16, F_ZERO);
            break;
        case 0:
            goto done;
        default:
            out_char(buf, n, &pos, '%');
            out_char(buf, n, &pos, *fmt);
            break;
        }
    }
done:
    if (buf && n) buf[pos < n ? pos : n - 1] = 0;
    return (int)pos;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

void kprintf_(const char *fmt, va_list ap) {
    char buf[512];
    int r = kvsnprintf(buf, sizeof(buf), fmt, ap);
    int n = r < (int)sizeof(buf) ? r : (int)sizeof(buf) - 1;
    for (int i = 0; i < n; i++) serial_putc(buf[i]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kprintf_(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...) {
    va_list ap;
    irq_disable();
    serial_puts("\n*** KERNEL PANIC: ");
    va_start(ap, fmt);
    kprintf_(fmt, ap);
    va_end(ap);
    serial_puts(" ***\n");
    for (;;) cpu_halt();
}
