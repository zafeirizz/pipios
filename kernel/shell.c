/* =========================================================================
 * AuroraOS desktop shell - taskbar, desktop icons, app launching
 *
 * The shell owns the "bare desktop" area (everything that is not a window):
 * it paints a taskbar along the bottom and icons down the left edge, and
 * handles clicks that fall outside every window.
 * ========================================================================= */
#include "apps.h"
#include "include/aurora.h"

#define BAR_H   28                       /* taskbar height */
#define ICO_W   88                       /* desktop icon hit box */
#define ICO_H   72

typedef struct {
    const char *label;
    int         kind;                    /* which app to launch */
    uint32_t    y;                       /* icon top on screen  */
} icon_t;

enum { APP_TERMINAL = 1, APP_FILES, APP_ABOUT };

static const icon_t icons[] = {
    { "Terminal", APP_TERMINAL, 24 },
    { "Files",    APP_FILES,    24 + ICO_H },
    { "About",    APP_ABOUT,    24 + 2 * ICO_H },
};
#define N_ICONS ((int)(sizeof(icons)/sizeof(icons[0])))

#define TB_X 8                            /* taskbar button origin */
#define TB_H (BAR_H - 8)

static void shell_launch(int kind) {
    compositor_t *c = gui_active;
    switch (kind) {
        case APP_TERMINAL: app_terminal_open(c, 120, 90); break;
        case APP_FILES:    app_filemanager_open(c, 200, 140, "/"); break;
        case APP_ABOUT: {
            window_t *w = gui_window_create(c, 300, 220, 380, 170,
                                            "About AuroraOS", NULL);
            if (w) gui_compose(c);
            break;
        }
    }
}

/* ---------------- painting ---------------- */

static void draw_icon(compositor_t *c, const icon_t *ic, int selected) {
    screen_t *s = &c->screen;
    uint32_t bx = 8, by = ic->y;
    /* icon plate */
    uint32_t plate = selected ? COLOR_FOCUS : rgb(0x35, 0x3a, 0x52);
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            (int32_t)bx + 24, (int32_t)by, 40, 34, plate, 1);
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            (int32_t)bx + 24, (int32_t)by, 40, 34, COLOR_BORDER, 0);
    /* label, centered-ish under the plate */
    int lw = (int)(strlen(ic->label) * FONT_ADVANCE);
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                (int32_t)bx + (ICO_W - lw) / 2, (int32_t)by + 38,
                ic->label, COLOR_TEXT, (uint32_t)-1);
}

static void draw_taskbar(compositor_t *c) {
    screen_t *s = &c->screen;
    uint32_t bar_y = s->height - BAR_H;
    /* bar */
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            0, (int32_t)bar_y, s->width, BAR_H, rgb(0x12, 0x12, 0x1c), 1);
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            0, (int32_t)bar_y, s->width, 1, COLOR_BORDER, 1);

    /* "Start" button */
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            TB_X, (int32_t)bar_y + 4, 74, TB_H, rgb(0x3d, 0x6e, 0xff), 1);
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                TB_X + 10, (int32_t)bar_y + 8, "Aurora", COLOR_WHITE,
                (uint32_t)-1);

    /* one task button per open window */
    int32_t tx = TB_X + 82;
    for (window_t *w = c->windows; w; w = w->next) {
        if (!w->vis) continue;
        uint32_t col = w->focused ? rgb(0x44, 0x4c, 0x6b) : rgb(0x24, 0x28, 0x3a);
        fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
                tx, (int32_t)bar_y + 4, 108, TB_H, col, 1);
        font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                    tx + 6, (int32_t)bar_y + 8, w->title, COLOR_TEXT,
                    (uint32_t)-1);
        tx += 114;
    }
}

/* ---------------- desktop proc (bare-area events + paint) ---------------- */

static void shell_proc(window_t *w, int32_t mx, int32_t my, int event) {
    (void)w;
    compositor_t *c = gui_active;
    screen_t *s = &c->screen;

    if (event == EV_MOVE) {
        for (int i = 0; i < N_ICONS; i++)
            draw_icon(c, &icons[i], 0);
        draw_taskbar(c);
        return;
    }

    if (event != EV_DOWN) return;

    uint32_t bar_y = s->height - BAR_H;

    /* Start button: raise every window, or if none, open the terminal */
    if (my >= (int32_t)bar_y + 4 && my < (int32_t)bar_y + 4 + TB_H &&
        mx >= TB_X && mx < TB_X + 74) {
        if (!c->windows) shell_launch(APP_TERMINAL);
        else for (window_t *w2 = c->windows; w2; w2 = w2->next)
                 w2->vis = 1;
        return;
    }

    /* task buttons: focus + raise the matching window */
    {
        int32_t tx = TB_X + 82;
        for (window_t *w2 = c->windows; w2; w2 = w2->next, tx += 114) {
            if (!w2->vis) continue;
            if (my >= (int32_t)bar_y + 4 && my < (int32_t)bar_y + 4 + TB_H &&
                mx >= tx && mx < tx + 108) {
                gui_window_set_focus(c, w2);
                gui_window_bring_forward(c, w2);
                return;
            }
        }
    }

    /* desktop icons */
    if (mx >= 8 && mx < 8 + ICO_W) {
        for (int i = 0; i < N_ICONS; i++) {
            if (my >= (int32_t)icons[i].y &&
                my < (int32_t)icons[i].y + ICO_H) {
                shell_launch(icons[i].kind);
                return;
            }
        }
    }
}

void shell_init(compositor_t *c) {
    c->desktop_proc = shell_proc;
    gui_compose(c);
}
