/* =========================================================================
 * AuroraOS file manager app - browse the FAT32 volume
 *
 * Click an entry to select it; click the selected entry again to open it
 * (directories navigate, files show a status line). The first row is
 * always ".." so you can walk back up.
 * ========================================================================= */
#include "apps.h"
#include "include/aurora.h"

#define FM_MAX 48          /* entries cached per directory */

typedef struct {
    char        path[128];
    fs_dirent_t ent[FM_MAX];
    int         n;
    int         sel;        /* selected row, -1 = none */
    char        status[96];
} fm_t;

static fm_t *FMof(window_t *w) { return (fm_t *)w->user; }

static void fm_load(fm_t *m) {
    m->n = 0;
    m->sel = -1;
    m->status[0] = 0;
    fs_dir_t d;
    if (fs_opendir(m->path, &d) != 0) {
        ksnprintf(m->status, sizeof(m->status), "cannot open %s", m->path);
        return;
    }
    fs_dirent_t e;
    while (m->n < FM_MAX && fs_readdir(&d, &e) == 0) {
        if (!strcmp(e.name, ".") || !strcmp(e.name, "..")) continue;
        m->ent[m->n++] = e;
    }
    if (!m->n) ksnprintf(m->status, sizeof(m->status), "(empty)");
}

/* go up one level: "/A/B" -> "/A", "/A" -> "/" */
static void fm_up(fm_t *m) {
    int len = (int)strlen(m->path);
    while (len > 1 && m->path[len - 1] == '/') len--;
    while (len > 1 && m->path[len - 1] != '/') len--;
    if (len <= 1) { m->path[0] = '/'; m->path[1] = 0; }
    else m->path[len] = 0;
    fm_load(m);
}

static void fm_enter(fm_t *m, int row) {
    if (row < 0 || row >= m->n) return;
    fs_dirent_t *e = &m->ent[row];
    if (!e->is_dir) {
        ksnprintf(m->status, sizeof(m->status),
                  "%s  %u bytes", e->name, e->size);
        return;
    }
    /* build the child path */
    int len = (int)strlen(m->path);
    int root = (len == 1 && m->path[0] == '/');
    int need = len + (root ? 0 : 1) + (int)strlen(e->name) + 1;
    if (need >= (int)sizeof(m->path)) return;
    if (!root) m->path[len++] = '/';
    for (const char *p = e->name; *p; p++) m->path[len++] = *p;
    m->path[len] = 0;
    fm_load(m);
}

static void fm_proc(window_t *w, int32_t mx, int32_t my, int event) {
    fm_t *m = FMof(w);
    compositor_t *c = gui_active;
    screen_t *s = &c->screen;

    if (event == EV_DOWN) {
        /* rows start just under the titlebar */
        int32_t row_y = (int32_t)TITLE_H + 2;
        int32_t rel   = my - row_y;
        if (rel >= 0) {
            int row = rel / (int32_t)aurora_font_h;
            if (row < m->n) {
                if (row == m->sel) fm_enter(m, row);   /* second click opens */
                else { m->sel = row; m->status[0] = 0; }
            }
        }
        (void)mx;
        return;
    }
    if (event == EV_KEY) {
        int ch = mx;
        if (ch == '\n' || ch == '\r') fm_enter(m, m->sel);
        else if (ch == '\b')          fm_up(m);
        else if (ch == 'k' && m->sel > 0) m->sel--;
        else if (ch == 'j' && m->sel < m->n - 1) m->sel++;
        return;
    }
    if (event != EV_MOVE) return;

    /* header: current path */
    int32_t cx = (int32_t)w->x + 4, cy = (int32_t)w->y + TITLE_H + 2;
    char hdr[144];
    ksnprintf(hdr, sizeof(hdr), "path: %s", m->path);
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                cx, cy, hdr, COLOR_FOCUS, COLOR_WIN);
    cy += (int32_t)aurora_font_h + 2;

    /* ".." row then the cached entries */
    int32_t rowh = (int32_t)aurora_font_h;
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                cx + 8, cy, "..", COLOR_TEXT, COLOR_WIN);
    cy += rowh;

    int32_t bottom = (int32_t)w->y + (int32_t)w->h - 2;
    for (int i = 0; i < m->n && cy + rowh <= bottom; i++) {
        fs_dirent_t *e = &m->ent[i];
        uint32_t fg = (i == m->sel) ? COLOR_WIN : COLOR_TEXT;
        uint32_t bg = (i == m->sel) ? COLOR_FOCUS : COLOR_WIN;
        char line[128];
        if (e->is_dir)
            ksnprintf(line, sizeof(line), "[%s]", e->name);
        else
            ksnprintf(line, sizeof(line), "%-12s %8u", e->name, e->size);
        font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                    cx + 8, cy, line, fg, bg);
        cy += rowh;
    }

    /* status line pinned to the bottom of the client area */
    if (m->status[0]) {
        int32_t sy = (int32_t)w->y + (int32_t)w->h - rowh - 2;
        font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                    cx, sy, m->status, COLOR_TEXT, COLOR_WIN);
    }
}

void app_filemanager_open(compositor_t *c, uint32_t x, uint32_t y,
                          const char *path) {
    window_t *w = gui_window_create(c, x, y, 460, 340,
                                    "Files", fm_proc);
    if (!w) return;
    fm_t *m = kzalloc(sizeof(fm_t));
    if (!m) { gui_window_destroy(c, w); return; }
    w->user = m;
    strncpy(m->path, path && *path ? path : "/", sizeof(m->path) - 1);
    fm_load(m);
    gui_compose(c);
}
