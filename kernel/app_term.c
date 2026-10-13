/* =========================================================================
 * AuroraOS terminal app - scrollback console with built-in commands
 * ========================================================================= */
#include "apps.h"
#include "include/aurora.h"

#define T_LINES 64          /* scrollback lines kept                */
#define T_COLS  62          /* characters per line                  */
#define T_PROMPT "aurora> "

typedef struct {
    char   lines[T_LINES][T_COLS];
    int    nrows;           /* total lines ever written             */
    char   in[T_COLS + 8];  /* current input line                   */
    int    inlen;
    int    inpos;           /* cursor index within input            */
} term_t;

static term_t *Tof(window_t *w) { return (term_t *)w->user; }

/* push one line into the scrollback (splits on \n) */
static void term_puts(term_t *t, const char *s) {
    char *ln = t->lines[t->nrows % T_LINES];
    int n = 0;
    for (; *s && n < T_COLS - 1; s++) {
        if (*s == '\n') {
            ln[n] = 0;
            t->nrows++;
            ln = t->lines[t->nrows % T_LINES];
            n = 0;
            continue;
        }
        ln[n++] = *s;
    }
    ln[n] = 0;
}

static void term_printf(term_t *t, const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    ksnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    term_puts(t, buf);
}

/* ---------------- commands ---------------- */

static void cmd_ls(term_t *t, const char *path) {
    fs_dir_t d;
    if (fs_opendir(path && *path ? path : "/", &d) != 0) {
        term_printf(t, "ls: cannot open %s", path && *path ? path : "/");
        return;
    }
    fs_dirent_t e;
    int shown = 0;
    while (fs_readdir(&d, &e) == 0 && shown < 12) {
        term_printf(t, "%c %8u  %s", e.is_dir ? 'd' : '-', e.size, e.name);
        shown++;
    }
    if (!shown) term_puts(t, "(empty)");
}

static void cmd_cat(term_t *t, const char *path) {
    fs_file_t f;
    if (fs_open(path, &f) != 0) { term_printf(t, "cat: no such file %s", path); return; }
    char buf[T_COLS];
    int got;
    while ((got = fs_read(&f, buf, sizeof(buf) - 1)) > 0) {
        buf[got] = 0;
        term_puts(t, buf);
    }
    fs_close(&f);
}

static void cmd_help(term_t *t) {
    term_puts(t, "commands:");
    term_puts(t, "  help              this list");
    term_puts(t, "  ls [path]         list a directory");
    term_puts(t, "  cat <file>        print a file");
    term_puts(t, "  echo <text>       print text");
    term_puts(t, "  clear             clear the screen");
    term_puts(t, "  free              free disk space");
    term_puts(t, "  mem               memory stats");
    term_puts(t, "  ver               version info");
}

static void cmd_run(window_t *w, char *line) {
    term_t *t = Tof(w);
    while (*line == ' ') line++;
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }

    if (!*line) return;
    if (!strcmp(line, "help"))        cmd_help(t);
    else if (!strcmp(line, "ls"))     cmd_ls(t, arg);
    else if (!strcmp(line, "cat"))    cmd_cat(t, arg);
    else if (!strcmp(line, "echo"))   term_puts(t, arg);
    else if (!strcmp(line, "clear"))  t->nrows = 0;
    else if (!strcmp(line, "free"))
        term_printf(t, "%u bytes free on FAT32", fs_free_bytes());
    else if (!strcmp(line, "mem"))
        term_printf(t, "%llu pages free of %llu",
                    (unsigned long long)pmm_free_pages_count(),
                    (unsigned long long)pmm_total_pages());
    else if (!strcmp(line, "ver"))
        term_puts(t, "AuroraOS 0.1 x86_64 (fat32, vesa, ps/2)");
    else term_printf(t, "%s: unknown command (try help)", line);
}

/* ---------------- window proc ---------------- */

static void term_proc(window_t *w, int32_t mx, int32_t my, int event) {
    (void)my;
    term_t *t = Tof(w);
    compositor_t *c = gui_active;
    screen_t *s = &c->screen;

    if (event == EV_KEY) {
        int ch = mx;                       /* mx carries the char */
        if (ch == '\n' || ch == '\r') {
            t->in[t->inlen] = 0;
            term_printf(t, "%s%s", T_PROMPT, t->in);
            char line[sizeof(t->in)];
            strncpy(line, t->in, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
            cmd_run(w, line);
            t->inlen = 0; t->inpos = 0; t->in[0] = 0;
        } else if (ch == '\b') {
            if (t->inpos > 0) {
                for (int i = t->inpos - 1; i < t->inlen - 1; i++)
                    t->in[i] = t->in[i + 1];
                t->inlen--; t->inpos--;
                t->in[t->inlen] = 0;
            }
        } else if (ch >= 32 && ch < 127 && t->inlen < (int)sizeof(t->in) - 2) {
            for (int i = t->inlen; i > t->inpos; i--) t->in[i] = t->in[i - 1];
            t->in[t->inpos++] = (char)ch;
            t->inlen++;
            t->in[t->inlen] = 0;
        }
        return;
    }

    if (event != EV_MOVE) return;

    /* repaint client area: scrollback + input line */
    int32_t cx = (int32_t)w->x + 2, cy = (int32_t)w->y + TITLE_H + 2;
    uint32_t cw = w->w - 4, ch_ = w->h - TITLE_H - 4;
    uint32_t rows = ch_ / aurora_font_h;
    if (rows < 2) return;

    uint32_t fg = COLOR_TEXT, bg = COLOR_WIN;
    /* last (rows-1) scrollback lines */
    int first = t->nrows - (int)rows + 1;
    if (first < 0) first = 0;
    for (uint32_t r = 0; r < rows - 1; r++) {
        int idx = first + (int)r;
        const char *txt = (idx < t->nrows && idx >= 0)
                          ? t->lines[idx % T_LINES] : "";
        font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                    cx, cy + (int32_t)(r * aurora_font_h), txt, fg, bg);
    }
    /* input line with prompt + cursor block */
    char iline[T_COLS + 16];
    ksnprintf(iline, sizeof(iline), "%s%s", T_PROMPT, t->in);
    int32_t iy = cy + (int32_t)((rows - 1) * aurora_font_h);
    font_string(c->buf, s->width, s->height, s->pitch, s->bpp,
                cx, iy, iline, fg, bg);
    int32_t curx = cx + (int32_t)(strlen(T_PROMPT) + t->inpos) * (int32_t)FONT_ADVANCE;
    fb_rect(c->buf, s->width, s->height, s->pitch, s->bpp,
            curx, iy + (int32_t)aurora_font_h - 3,
            FONT_ADVANCE, 2, fg, 1);
    (void)cw;
}

void app_terminal_open(compositor_t *c, uint32_t x, uint32_t y) {
    window_t *w = gui_window_create(c, x, y, 520, 300, "Terminal", term_proc);
    if (!w) return;
    term_t *t = kzalloc(sizeof(term_t));
    if (!t) { gui_window_destroy(c, w); return; }
    w->user = t;
    term_puts(t, "AuroraOS terminal");
    term_puts(t, "type 'help' for commands");
    gui_window_set_focus(c, w);
    gui_compose(c);
}
