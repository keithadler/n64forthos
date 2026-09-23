/* Text console: a grid of 8x16 glyphs drawn straight into the framebuffer,
 * with a copy of the text kept beside it, so that whatever paints over the
 * console -- the editor, the desktop, an app -- can hand the screen back and
 * the console can put its scrollback where it was. */
#include <stdarg.h>
#include "n64.h"
#include "font.h"

#define FONT_W FONT_W_PX
#define FONT_H FONT_H_PX
/* Two cells of margin either side: a television throws away the edges of the
 * picture, and browser cores crop them too, so text starts inside the safe
 * area rather than at pixel zero. */
#define MARGIN 2
/* The console is a window on the left of the screen, not the whole screen:
 * scrolling copies pixels, and anything drawn beside it -- a panel, a window
 * -- would be dragged along with the text.  Everything right of CON_W
 * belongs to whoever drew it. */
#define ROWS (SCREEN_H / FONT_H)                  /* 30 */
#define CON_X org_x                               /* 16, full screen */
#define CON_W (cols * FONT_W)                     /* 384 at 48 columns */

/* Where the console's column 0 and row 0 are on the screen.  Full screen,
 * two cells in from the left; in a window, wherever the window is. */
static int org_x = MARGIN * FONT_W, org_y;

/* 48 columns leaves the right of the screen to whatever is drawn there; a
 * prompt with a real keyboard, and no on-screen one, takes 76. */
static int cols = 48;

extern u16 *fb_uncached(void);

static u16 *fb;
static u16 fg, bg;
static char text[ROWS][CON_MAX_COLS];
static u16 ink[ROWS][CON_MAX_COLS];
static int cur_row, cur_col;
static int top_row = 2, bot_row = ROWS - 2;

static void fill(int x, int y, int w, int h, u16 c)
{
    gfx_box(x, y, w, h, c);
}

static void draw_glyph(int row, int col, char ch, u16 f, u16 b)
{
    gfx_glyph(org_x + col * FONT_W, org_y + row * FONT_H, ch, f, b, 1);
    if (row >= 0 && row < ROWS && col >= 0 && col < CON_MAX_COLS) {
        text[row][col] = ch;
        ink[row][col] = f;
    }
}

static void forget_row(int row)
{
    int c;

    for (c = 0; c < CON_MAX_COLS; c++)
        text[row][c] = ' ';
}

/* Paint the console's rows from the copy: after something else has had the
 * screen, or after the width changed. */
void con_redraw(void)
{
    int r, c;

    gfx_cursor_hide();
    fill(CON_X, org_y + top_row * FONT_H, CON_W,
         (bot_row - top_row + 1) * FONT_H, bg);
    for (r = top_row; r <= bot_row; r++)
        for (c = 0; c < cols; c++)
            if (text[r][c] != ' ' && text[r][c])
                gfx_glyph(org_x + c * FONT_W, org_y + r * FONT_H, text[r][c],
                          ink[r][c], bg, 1);
}

/* The status bar is the width of the screen, not of the console. */
#define BAR_COLS (SCREEN_W / FONT_W)            /* 80 */

static void draw_bar_glyph(int col, char ch, u16 f, u16 b)
{
    gfx_glyph(col * FONT_W, 0, ch, f, b, 1);
}

void con_init(u16 background)
{
    int r;

    for (r = 0; r < ROWS; r++)
        forget_row(r);
    fb = fb_uncached();
    bg = background;
    fg = RGB(200, 210, 225);
    gfx_cls(bg);
    cur_row = top_row;
    cur_col = 0;
}

void con_color(u16 f) { fg = f; }
u16 con_get_color(void) { return fg; }
int con_row(void) { return cur_row; }
int con_col(void) { return cur_col; }

void con_erase_row(int row)
{
    gfx_cursor_hide();
    fill(CON_X, org_y + row * FONT_H, CON_W, FONT_H, bg);
    if (row >= 0 && row < ROWS)
        forget_row(row);
}

/* Put the console somewhere else: its column 0 and row 0 at (x, y), this
 * many columns, and rows top to bottom.  The text comes along. */
void con_place(int x, int y, int ncols, int top, int bottom)
{
    org_x = x;
    org_y = y;
    con_set_cols(ncols);
    con_scroll_region(top, bottom);
}

int con_origin_x(void) { return org_x; }
int con_origin_y(void) { return org_y; }

void con_set_cols(int n)
{
    cols = n < 8 ? 8 : n > CON_MAX_COLS ? CON_MAX_COLS : n;
    if (cur_col > cols)
        cur_col = cols;
}

int con_cols(void)
{
    return cols;
}

void con_scroll_region(int top, int bottom)
{
    /* A smaller region keeps the bottom of what was there: the lines the
     * cursor is on, not the oldest ones. */
    if (cur_row > bottom) {
        int shift = cur_row - bottom, r, c;

        for (r = top; r <= bottom; r++)
            for (c = 0; c < CON_MAX_COLS; c++) {
                int from = r + shift;

                text[r][c] = from < ROWS ? text[from][c] : ' ';
                ink[r][c] = from < ROWS ? ink[from][c] : fg;
            }
        for (r = bottom + 1; r < ROWS; r++)
            forget_row(r);
        cur_row = bottom;
    }
    top_row = top;
    bot_row = bottom;
    if (cur_row < top_row)
        cur_row = top_row;
}

void con_at(int row, int col)
{
    cur_row = row;
    cur_col = col;
}

void con_clear(void)
{
    int r;

    gfx_cursor_hide();
    fill(CON_X, org_y + top_row * FONT_H, CON_W,
         (bot_row - top_row + 1) * FONT_H, bg);
    for (r = top_row; r <= bot_row; r++)
        forget_row(r);
    cur_row = top_row;
    cur_col = 0;
}

/* Scroll the console band only -- its own columns, nobody else's. */
static void scroll(void)
{
    int y, x;
    int lines = (bot_row - top_row) * FONT_H;
    u16 *base = fb + (org_y + top_row * FONT_H) * SCREEN_W + CON_X;

    if (!(CON_X & 1)) {                 /* two pixels a word */
        u32 *dst = (u32 *)base;

        for (y = 0; y < lines; y++) {
            u32 *d = dst + y * (SCREEN_W / 2);
            const u32 *s = d + FONT_H * (SCREEN_W / 2);

            for (x = 0; x < CON_W / 2; x++)
                d[x] = s[x];
        }
    } else {                            /* a window at an odd x */
        for (y = 0; y < lines; y++) {
            u16 *d = base + y * SCREEN_W;
            const u16 *s = d + FONT_H * SCREEN_W;

            for (x = 0; x < CON_W; x++)
                d[x] = s[x];
        }
    }
    fill(CON_X, org_y + bot_row * FONT_H, CON_W, FONT_H, bg);
    for (y = top_row; y < bot_row; y++)
        for (x = 0; x < CON_MAX_COLS; x++) {
            text[y][x] = text[y + 1][x];
            ink[y][x] = ink[y + 1][x];
        }
    forget_row(bot_row);
    cur_row = bot_row;
}

void con_putc(char c)
{
    /* The pointer comes down first: scrolling would copy it into the text,
     * and putting it back later would rub out what was drawn under it.
     * Whoever showed it shows it again next frame. */
    gfx_cursor_hide();
    if (c == '\n') {
        cur_col = 0;
        if (++cur_row > bot_row)
            scroll();
        return;
    }
    if (c == '\r') {
        cur_col = 0;
        return;
    }
    if (c == '\t') {
        do {
            con_putc(' ');
        } while (cur_col & 7);
        return;
    }
    if (cur_col >= cols) {
        cur_col = 0;
        if (++cur_row > bot_row)
            scroll();
    }
    draw_glyph(cur_row, cur_col++, c, fg, bg);
}

void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

/* Top-line status bar, in inverse video.  Only the cells that actually
 * changed are repainted: it is redrawn every frame, and blanking the whole
 * bar first would flicker on a television and tear in a screenshot. */
void con_status(const char *s)
{
    static char shown[BAR_COLS + 1];
    static int painted;
    u16 sf = RGB(16, 20, 40), sb = RGB(120, 200, 255);
    int col;

    if (!painted) {
        fill(0, 0, SCREEN_W, FONT_H, sb);
        for (col = 0; col < BAR_COLS; col++)
            shown[col] = ' ';
        painted = 1;
    }
    for (col = 0; col < BAR_COLS; col++) {
        char c = *s ? *s++ : ' ';

        if (c != shown[col]) {
            draw_bar_glyph(col, c, sf, sb);
            shown[col] = c;
        }
    }
}

static void put_unsigned(u32 v, u32 base, int width, char pad)
{
    char buf[12];
    int n = 0;

    do {
        u32 d = v % base;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (n < width)
        buf[n++] = pad;
    while (n--)
        con_putc(buf[n]);
}

void con_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        int width = 0;
        char pad = ' ';

        if (*fmt != '%') {
            con_putc(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        switch (*fmt) {
        case 'c': con_putc((char)va_arg(ap, int)); break;
        case 's': con_puts(va_arg(ap, const char *)); break;
        case 'u': put_unsigned(va_arg(ap, u32), 10, width, pad); break;
        case 'x': put_unsigned(va_arg(ap, u32), 16, width, pad); break;
        case 'd': {
            s32 v = va_arg(ap, s32);
            if (v < 0) {
                con_putc('-');
                v = -v;
            }
            put_unsigned((u32)v, 10, width, pad);
            break;
        }
        case '%': con_putc('%'); break;
        default: con_putc('%'); con_putc(*fmt); break;
        }
    }
    va_end(ap);
}
