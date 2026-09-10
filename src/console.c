/* Text console: an 80x30 grid of 8x16 glyphs drawn straight into the
 * framebuffer.  No backing text buffer - the pixels are the state. */
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
#define COLS 48
#define ROWS (SCREEN_H / FONT_H)                  /* 30 */
#define CON_X (MARGIN * FONT_W)                   /* 16 */
#define CON_W (COLS * FONT_W)                     /* 384 */

extern u16 *fb_uncached(void);

static u16 *fb;
static u16 fg, bg;
static int cur_row, cur_col;
static int top_row = 2, bot_row = ROWS - 2;

static void fill(int x, int y, int w, int h, u16 c)
{
    gfx_box(x, y, w, h, c);
}

static void draw_glyph(int row, int col, char ch, u16 f, u16 b)
{
    gfx_glyph((col + MARGIN) * FONT_W, row * FONT_H, ch, f, b, 1);
}

/* The status bar is the width of the screen, not of the console. */
#define BAR_COLS (SCREEN_W / FONT_W)            /* 80 */

static void draw_bar_glyph(int col, char ch, u16 f, u16 b)
{
    gfx_glyph(col * FONT_W, 0, ch, f, b, 1);
}

void con_init(u16 background)
{
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
    fill(CON_X, row * FONT_H, CON_W, FONT_H, bg);
}

void con_scroll_region(int top, int bottom)
{
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
    fill(CON_X, top_row * FONT_H, CON_W,
         (bot_row - top_row + 1) * FONT_H, bg);
    cur_row = top_row;
    cur_col = 0;
}

/* Scroll the console band only -- its own columns, nobody else's. */
static void scroll(void)
{
    int y, x;
    int lines = (bot_row - top_row) * FONT_H;
    u16 *dst = fb + top_row * FONT_H * SCREEN_W + CON_X;

    for (y = 0; y < lines; y++) {
        u16 *d = dst + y * SCREEN_W;
        const u16 *s = d + FONT_H * SCREEN_W;

        for (x = 0; x < CON_W; x++)
            d[x] = s[x];
    }
    fill(CON_X, bot_row * FONT_H, CON_W, FONT_H, bg);
    cur_row = bot_row;
}

void con_putc(char c)
{
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
    if (cur_col >= COLS) {
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
