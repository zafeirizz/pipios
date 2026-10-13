/* =========================================================================
 * AuroraOS GUI - framebuffer pixel layer + font + cursor sprite
 * ========================================================================= */
#include "include/gui.h"
#include "include/aurora.h"

/* ---- arrow cursor: 16x16, 2 bytes/row, MSB = leftmost pixel --------- */
static const uint8_t arrow_cursor_bits[32] = {
    0x80,0x00, 0xc0,0x00, 0xe0,0x00, 0xf0,0x00,
    0xf8,0x00, 0xfc,0x00, 0xfe,0x00, 0xff,0x00,
    0xff,0x80, 0xff,0xc0, 0x78,0x00, 0x3c,0x00,
    0x1e,0x00, 0x0f,0x00, 0x07,0x80, 0x03,0xc0,
};

/* ---------------- screen layer ---------------- */

int screen_init(boot_info_t *bi, screen_t *s) {
    if (!bi || bi->bpp != 32) return -1;
    s->width  = bi->width;
    s->height = bi->height;
    s->pitch  = bi->pitch;
    s->bpp    = 32;
    s->bytes  = 4;
    s->phys   = bi->lfb;
    /* stage2 identity-maps the first 4 GiB, which covers the LFB. */
    s->fb = (void *)(uintptr_t)bi->lfb;
    return 0;
}

/* Copy a rectangle from s->fb to dst_fb (both 32bpp). */
void screen_flush(const screen_t *s, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, void *dst_fb) {
    if (!s || !dst_fb || s->bpp != 32) return;
    if (x >= s->width || y >= s->height) return;
    if (x + w > s->width)  w = s->width - x;
    if (y + h > s->height) h = s->height - y;
    for (uint32_t row = 0; row < h; row++) {
        const uint32_t *src = (const uint32_t *)
            ((const uint8_t *)s->fb + (y + row) * s->pitch) + x;
        uint32_t *dst = (uint32_t *)((uint8_t *)dst_fb + (y + row) * s->pitch) + x;
        for (uint32_t col = 0; col < w; col++) dst[col] = src[col];
    }
}

/* ---------------- pixel primitives (32bpp) ---------------- */

void fb_fill(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp,
             uint32_t color) {
    if (bpp != 32) return;
    uint8_t *row = (uint8_t *)fb;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *px = (uint32_t *)row;
        for (uint32_t x = 0; x < w; x++) px[x] = color;
        row += pitch;
    }
}

void fb_pixel(void *fb, uint32_t pitch, uint32_t bpp,
              uint32_t x, uint32_t y, uint32_t color) {
    if (bpp != 32) return;
    *(uint32_t *)((uint8_t *)fb + y * pitch + x * 4) = color;
}

/* rect: outline (filled=0) or solid (filled=1); negative x/y allowed,
 * the caller clamps by bounds we enforce here. */
void fb_rect(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp,
             int32_t x, int32_t y, uint32_t w_, uint32_t h_, uint32_t color,
             int filled) {
    if (bpp != 32) return;
    if (x < 0) { if ((uint32_t)(-x) >= w_) return; w_ += x; x = 0; }
    if (y < 0) { if ((uint32_t)(-y) >= h_) return; h_ += y; y = 0; }
    if ((uint32_t)x >= w || (uint32_t)y >= h) return;
    if ((uint32_t)x + w_ > w) w_ = w - (uint32_t)x;
    if ((uint32_t)y + h_ > h) h_ = h - (uint32_t)y;
    if (!w_ || !h_) return;

    uint8_t *base = (uint8_t *)fb + (uint32_t)y * pitch;
    if (filled) {
        for (uint32_t row = 0; row < h_; row++) {
            uint32_t *px = (uint32_t *)(base + row * pitch) + x;
            for (uint32_t col = 0; col < w_; col++) px[col] = color;
        }
    } else {
        uint32_t *top = (uint32_t *)base + x;
        uint32_t *bot = (uint32_t *)(base + (h_ - 1) * pitch) + x;
        for (uint32_t col = 0; col < w_; col++) { top[col] = color; bot[col] = color; }
        for (uint32_t row = 0; row < h_; row++) {
            uint32_t *px = (uint32_t *)(base + row * pitch) + x;
            px[0] = color;
            if (w_ > 1) px[w_ - 1] = color;
        }
    }
}

/* Bresenham line, clipped to the buffer rectangle. */
void fb_line(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp,
             int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (bpp != 32) return;
    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < w && (uint32_t)y0 < h)
            *(uint32_t *)((uint8_t *)fb + (uint32_t)y0 * pitch + (uint32_t)x0 * 4)
                = color;
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ---------------- PSF font rendering ---------------- */

font_info_t font_info(void) {
    font_info_t fi;
    fi.width = aurora_font_w; fi.height = aurora_font_h;
    fi.advance = FONT_ADVANCE; fi.first = 0;
    return fi;
}

/* Draw one glyph with fg/bg (bg = -1u means transparent background). */
void font_draw(void *fb, uint32_t w, uint32_t h, uint32_t pitch,
               uint32_t bpp, int32_t x, int32_t y, uint32_t glyph,
               uint32_t fg, uint32_t bg) {
    (void)w; (void)h;
    if (bpp != 32 || glyph >= FONT_GLYPHS) return;
    const uint8_t *g = aurora_font + glyph * aurora_font_h;
    for (uint32_t row = 0; row < aurora_font_h; row++) {
        int32_t py = y + (int32_t)row;
        if (py < 0 || (uint32_t)py >= h) continue;
        uint8_t bits = g[row];
        uint32_t *px = (uint32_t *)((uint8_t *)fb + (uint32_t)py * pitch) + x;
        for (uint32_t col = 0; col < aurora_font_w; col++) {
            int32_t pxx = x + (int32_t)col;
            if (pxx < 0 || (uint32_t)pxx >= w) continue;
            if (bits & (0x80u >> col)) px[col] = fg;
            else if (bg != (uint32_t)-1) px[col] = bg;
        }
    }
}

void font_string(void *fb, uint32_t w, uint32_t h, uint32_t pitch,
                 uint32_t bpp, int32_t x, int32_t y, const char *s,
                 uint32_t fg, uint32_t bg) {
    int32_t cx = x;
    for (const char *p = s; *p; p++) {
        uint32_t g = (uint8_t)*p;
        if (g >= FONT_GLYPHS) g = '?';
        font_draw(fb, w, h, pitch, bpp, cx, y, g, fg, bg);
        cx += (int32_t)FONT_ADVANCE;
    }
}

int font_measure(const char *s) {
    return (int)(strlen(s) * FONT_ADVANCE);
}

/* ---------------- compositor ---------------- */

/* TITLE_H/WIN_PAD/CLOSE_W/CLOSE_H come from include/gui.h */

/* The compositor every window proc can reach (single desktop). */
compositor_t *gui_active;

/* left edge of the close box in screen coords */
static inline uint32_t close_box_x(const window_t *w) {
    return w->x + w->w - CLOSE_W - 4;
}
static inline uint32_t close_box_y(const window_t *w) {
    return w->y + (TITLE_H - CLOSE_H) / 2;
}

static void paint_wallpaper(compositor_t *c) {
    /* subtle vertical gradient so screendumps are visually checkable */
    uint8_t *row = (uint8_t *)c->buf;
    for (uint32_t y = 0; y < c->screen.height; y++) {
        uint8_t v = (uint8_t)(0x1f + (y * 0x20) / c->screen.height);
        uint32_t col = rgb(v, v, (uint8_t)(v + 0x0f));
        uint32_t *px = (uint32_t *)row;
        for (uint32_t x = 0; x < c->screen.width; x++) px[x] = col;
        row += c->screen.pitch;
    }
    /* desktop banner text */
    font_string(c->buf, c->screen.width, c->screen.height,
                c->screen.pitch, c->screen.bpp,
                12, (int32_t)c->screen.height - 28,
                "AuroraOS 0.1 - x86_64 - FAT32 - GUI online",
                rgb(0x60, 0x70, 0x90), (uint32_t)-1);
}

int gui_init(compositor_t *c, boot_info_t *bi) {
    memset(c, 0, sizeof(*c));
    if (screen_init(bi, &c->screen) != 0) return -1;
    /* The offscreen desktop is one full frame (e.g. 4096*768 = 3 MiB at
     * 1024x768 with 32bpp) — far larger than the kernel heap, so take it
     * straight from the PMM. Paging identity-maps the first 4 GiB, and the
     * PMM only hands out usable RAM below 256 MiB, so the physical address
     * is directly usable as a virtual pointer (same trick heap_init uses). */
    size_t size = (size_t)c->screen.pitch * c->screen.height;
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) return -2;
    c->buf = (void *)(uintptr_t)phys;
    c->buf_pitch = c->screen.pitch;
    paint_wallpaper(c);
    c->cursor_bits = arrow_cursor_bits;
    c->cursor_w = 16; c->cursor_h = 16;
    c->cursor_hot_x = 0; c->cursor_hot_y = 0;
    c->cursor_vis = 1;
    c->cursor_x = c->screen.width / 2;
    c->cursor_y = c->screen.height / 2;
    gui_active = c;
    return 0;
}

void gui_shutdown(compositor_t *c) {
    while (c->windows) gui_window_destroy(c, c->windows);
    if (c->buf) {
        size_t size = (size_t)c->screen.pitch * c->screen.height;
        pmm_free_pages((uint64_t)(uintptr_t)c->buf,
                       (size + PAGE_SIZE - 1) / PAGE_SIZE);
        c->buf = NULL;
    }
}

void gui_flush(compositor_t *c) {
    screen_t *s = &c->screen;
    memcpy(s->fb, c->buf, (size_t)s->pitch * s->height);
}

/* ---------------- window lifecycle ---------------- */

window_t *gui_window_create(compositor_t *c, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, const char *title,
                            wp_t proc) {
    window_t *win = kzalloc(sizeof(window_t));
    if (!win) return NULL;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->vis = 1;
    strncpy(win->title, title ? title : "", sizeof(win->title) - 1);
    win->proc = proc;
    /* new windows go on top of the z-order (front of the list) */
    win->next = c->windows;
    c->windows = win;
    if (!c->focus) gui_window_set_focus(c, win);
    return win;
}

void gui_window_destroy(compositor_t *c, window_t *w) {
    if (!w) return;
    if (w->proc) w->proc(w, 0, 0, EV_DESTROY);
    window_t **pp = &c->windows;
    while (*pp) {
        if (*pp == w) { *pp = w->next; break; }
        pp = &(*pp)->next;
    }
    if (c->focus == w) c->focus = NULL;
    kfree(w);
}

void gui_window_show(compositor_t *c, window_t *w) { (void)c; w->vis = 1; }
void gui_window_hide(compositor_t *c, window_t *w) { (void)c; w->vis = 0; }

int gui_window_move(compositor_t *c, window_t *w, uint32_t x, uint32_t y) {
    (void)c;
    if (x + w->w > c->screen.width)  return -1;
    if (y + w->h > c->screen.height) return -1;
    w->x = x; w->y = y;
    return 0;
}

int gui_window_resize(compositor_t *c, window_t *w, uint32_t w_, uint32_t h_) {
    (void)c;
    if (w_ < 40 || h_ < 30) return -1;
    if (w->x + w_ > c->screen.width || w->y + h_ > c->screen.height)
        return -1;
    w->w = w_; w->h = h_;
    return 0;
}

void gui_window_set_focus(compositor_t *c, window_t *w) {
    if (c->focus == w) return;
    if (c->focus) c->focus->focused = 0;
    c->focus = w;
    if (w) w->focused = 1;
}

/* move w to the front of the z-order */
void gui_window_bring_forward(compositor_t *c, window_t *w) {
    if (!w || c->windows == w) return;
    window_t **pp = &c->windows;
    while (*pp) {
        if (*pp == w) { *pp = w->next; break; }
        pp = &(*pp)->next;
    }
    w->next = c->windows;
    c->windows = w;
}

/* paint one window (chrome + client area) into the offscreen buffer */
void gui_window_paint(compositor_t *c, window_t *w) {
    if (!w->vis) return;
    screen_t *s = &c->screen;
    /* border */
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            (int32_t)w->x, (int32_t)w->y, w->w, w->h, COLOR_BORDER, 0);
    /* titlebar */
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            (int32_t)w->x + 1, (int32_t)w->y + 1,
            w->w - 2, TITLE_H - 2,
            w->focused ? COLOR_FOCUS : COLOR_TITLE, 1);
    /* title text */
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                (int32_t)w->x + 6, (int32_t)w->y + 2,
                w->title, COLOR_TEXT, (uint32_t)-1);
    /* close box */
    {
        int32_t bx = (int32_t)close_box_x(w), by = (int32_t)close_box_y(w);
        fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
                bx, by, CLOSE_W, CLOSE_H, COLOR_TEXT, 0);
        font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                    bx + 4, by - 1, "x", COLOR_TEXT, (uint32_t)-1);
    }
    /* client area */
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            (int32_t)w->x + 1, (int32_t)w->y + TITLE_H,
            w->w - 2, w->h - TITLE_H - 1, COLOR_WIN, 1);
    /* let the window draw its own client content */
    if (w->proc)
        w->proc(w, (int32_t)(w->x + WIN_PAD),
                (int32_t)(w->y + TITLE_H + WIN_PAD), EV_MOVE);
}

/* ---------------- cursor ---------------- */

void gui_draw_cursor(compositor_t *c) {
    if (!c->cursor_vis) return;
    int32_t cx = (int32_t)c->cursor_x - (int32_t)c->cursor_hot_x;
    int32_t cy = (int32_t)c->cursor_y - (int32_t)c->cursor_hot_y;
    for (uint32_t j = 0; j < c->cursor_h; j++) {
        int32_t yy = cy + (int32_t)j;
        if (yy < 0 || yy >= (int32_t)c->screen.height) continue;
        uint8_t bits = c->cursor_bits[j * 2];   /* only 8 px wide needed */
        uint8_t bits2 = c->cursor_bits[j * 2 + 1];
        for (uint32_t i = 0; i < c->cursor_w; i++) {
            int32_t xx = cx + (int32_t)i;
            if (xx < 0 || xx >= (int32_t)c->screen.width) continue;
            uint8_t b = (i < 8) ? bits : bits2;
            if (b & (0x80u >> (i % 8)))
                fb_pixel(c->buf, c->screen.pitch, c->screen.bpp,
                         (uint32_t)xx, (uint32_t)yy, COLOR_BLACK);
        }
    }
}

/* ---------------- mouse-driven desktop ---------------- */

static window_t *find_window(compositor_t *c, uint32_t x, uint32_t y) {
    /* list is front-first; return the topmost visible hit */
    for (window_t *w = c->windows; w; w = w->next) {
        if (!w->vis) continue;
        if (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h)
            return w;
    }
    return NULL;
}

uint32_t gui_poll_mouse(compositor_t *c) {
    uint32_t events = 0;
    int32_t mx = mouse_x(), my = mouse_y();
    uint8_t  but = mouse_buttons();

    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= (int32_t)c->screen.width)  mx = (int32_t)c->screen.width - 1;
    if (my >= (int32_t)c->screen.height) my = (int32_t)c->screen.height - 1;
    c->cursor_x = (uint32_t)mx;
    c->cursor_y = (uint32_t)my;

    static uint8_t prev_but = 0;
    uint8_t pressed  = (uint8_t)(but & ~prev_but);
    uint8_t released = (uint8_t)(prev_but & ~but);
    prev_but = but;

    window_t *hit = find_window(c, c->cursor_x, c->cursor_y);
    if (pressed & 1) {
        if (hit && c->cursor_y < hit->y + TITLE_H &&
            c->cursor_x >= close_box_x(hit) &&
            c->cursor_x <  close_box_x(hit) + CLOSE_W) {
            /* click on the titlebar close box */
            gui_window_destroy(c, hit);
            gui_compose(c);
            return 1;
        }
        if (hit) {
            gui_window_set_focus(c, hit);
            gui_window_bring_forward(c, hit);
            /* titlebar drag */
            if (c->cursor_y < hit->y + TITLE_H) {
                hit->drag = 1;
                hit->drag_off_x = c->cursor_x - hit->x;
                hit->drag_off_y = c->cursor_y - hit->y;
            }
            if (hit->proc) {
                hit->proc(hit,
                          (int32_t)(c->cursor_x - hit->x),
                          (int32_t)(c->cursor_y - hit->y), EV_DOWN);
            }
            events++;
        } else if (!hit && c->desktop_proc) {
            c->desktop_proc(NULL,
                            (int32_t)c->cursor_x, (int32_t)c->cursor_y,
                            EV_DOWN);
            events++;
        }
    }
    if (released & 1) {
        window_t *dragging = c->windows;
        while (dragging) { dragging->drag = 0; dragging = dragging->next; }
        if (hit && hit->proc) {
            hit->proc(hit,
                      (int32_t)(c->cursor_x - hit->x),
                      (int32_t)(c->cursor_y - hit->y), EV_UP);
        } else if (!hit && c->desktop_proc) {
            c->desktop_proc(NULL,
                            (int32_t)c->cursor_x, (int32_t)c->cursor_y,
                            EV_UP);
        }
        events++;
    }
    if (events == 0 && mouse_events() == 0 && pressed == 0 && released == 0) {
        /* nothing changed since last frame */
        return 0;
    }

    /* apply dragging before painting */
    for (window_t *w = c->windows; w; w = w->next) {
        if (!w->drag) continue;
        int32_t nx = (int32_t)c->cursor_x - (int32_t)w->drag_off_x;
        int32_t ny = (int32_t)c->cursor_y - (int32_t)w->drag_off_y;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        gui_window_move(c, w, (uint32_t)nx, (uint32_t)ny);
        events++;
    }

    gui_compose(c);
    return events ? events : 1;
}

/* Full recompose: wallpaper, desktop, windows back-to-front, cursor. */
void gui_compose(compositor_t *c) {
    paint_wallpaper(c);
    if (c->desktop_proc)
        c->desktop_proc(NULL, 0, 0, EV_MOVE);   /* paint the desktop area */
    /* window list is front-first; paint tail -> head */
    window_t *stack[64];
    int n = 0;
    for (window_t *w = c->windows; w && n < 64; w = w->next) stack[n++] = w;
    for (int i = n - 1; i >= 0; i--) gui_window_paint(c, stack[i]);
    gui_draw_cursor(c);
    gui_flush(c);
}

void gui_notify_key(compositor_t *c, int ch) {
    if (!c->focus || !c->focus->proc) return;
    /* mx carries the ASCII char for EV_KEY */
    c->focus->proc(c->focus, ch, 0, EV_KEY);
}

/* ---------------- built-in cursor accessors ---------------- */

const uint8_t *gui_arrow_cursor_bits(void) { return arrow_cursor_bits; }
uint32_t gui_arrow_cursor_w(void)     { return 16; }
uint32_t gui_arrow_cursor_h(void)     { return 16; }
uint32_t gui_arrow_cursor_hot_x(void) { return 0; }
uint32_t gui_arrow_cursor_hot_y(void) { return 0; }
