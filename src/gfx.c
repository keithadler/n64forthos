/* gfx.c -- the drawing layer.
 *
 * Everything that puts pixels in the framebuffer lives here: the console
 * calls it for glyphs, Forth calls it through DRAW-TEXT, BLIT-SPRITE and
 * friends, and the window system will call it when there is one.  Writes go
 * through the uncached alias, so nothing has to be flushed before the video
 * interface reads it.
 */
#include "n64.h"
#include "font.h"

u16 *gfx_fb(void)
{
    extern u16 *fb_uncached(void);
    return fb_uncached();
}

void gfx_plot(int x, int y, u16 c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        gfx_fb()[y * SCREEN_W + x] = c;
}

void gfx_box(int x, int y, int w, int h, u16 c)
{
    u16 *fb = gfx_fb();
    int i, j;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (j = 0; j < h; j++) {
        u16 *p = fb + (y + j) * SCREEN_W + x;
        for (i = 0; i < w; i++)
            p[i] = c;
    }
}

void gfx_cls(u16 c)
{
    gfx_box(0, 0, SCREEN_W, SCREEN_H, c);
}

void gfx_hline(int x, int y, int w, u16 c)
{
    gfx_box(x, y, w, 1, c);
}

void gfx_vline(int x, int y, int h, u16 c)
{
    gfx_box(x, y, 1, h, c);
}

void gfx_frame(int x, int y, int w, int h, u16 c)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

void gfx_line(int x0, int y0, int x1, int y1, u16 c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err;

    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    err = dx - dy;
    for (;;) {
        gfx_plot(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            return;
        {
            int e2 = err << 1;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    }
}

/* One glyph at pixel coordinates.  opaque paints the background too, which is
 * what the console wants; transparent is what text over graphics wants. */
void gfx_glyph(int x, int y, char ch, u16 fg, u16 bg, int opaque)
{
    const unsigned char *g;
    int idx = (unsigned char)ch - FONT_FIRST;
    int row, col;

    if (idx < 0 || idx >= FONT_COUNT)
        idx = '?' - FONT_FIRST;
    g = font8x16[idx];
    for (row = 0; row < FONT_H_PX; row++) {
        unsigned bits = g[row];
        int py = y + row;

        if ((unsigned)py >= SCREEN_H)
            continue;
        for (col = 0; col < FONT_W_PX; col++) {
            int px = x + col;

            if ((unsigned)px >= SCREEN_W)
                continue;
            if (bits & (0x80u >> col))
                gfx_fb()[py * SCREEN_W + px] = fg;
            else if (opaque)
                gfx_fb()[py * SCREEN_W + px] = bg;
        }
    }
}

/* Returns the x it ended at, so callers can chain. */
int gfx_text(int x, int y, const char *s, int len, u16 c)
{
    while (len-- > 0) {
        gfx_glyph(x, y, *s++, c, 0, 0);
        x += FONT_W_PX;
    }
    return x;
}

/* A sprite is just 16-bit pixels, row major.  keyed treats a zero pixel --
 * one with the 5551 alpha bit clear -- as transparent. */
void gfx_blit(const u16 *src, int x, int y, int w, int h, int keyed)
{
    u16 *fb = gfx_fb();
    int i, j;

    for (j = 0; j < h; j++) {
        int py = y + j;

        if ((unsigned)py >= SCREEN_H)
            continue;
        for (i = 0; i < w; i++) {
            int px = x + i;
            u16 p = src[j * w + i];

            if ((unsigned)px >= SCREEN_W)
                continue;
            if (keyed && !(p & 1))
                continue;
            fb[py * SCREEN_W + px] = p;
        }
    }
}

/* ------------------------------------------------------------- pointer
 *
 * A mouse pointer has to be drawn over whatever is already there and taken
 * away again without a repaint, so the pixels underneath it are kept.
 */
#define CUR_W 10
#define CUR_H 16

static const u16 cursor_bits[CUR_H] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFE00, 0xF600, 0xE600, 0x0300, 0x0300, 0x0180,
};
static const u16 cursor_mask[CUR_H] = {   /* one pixel wider: the outline */
    0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80,
    0xFFC0, 0xFFE0, 0xFFC0, 0xFF80, 0xF780, 0xE780, 0x03C0, 0x03C0,
};

static u16 under[CUR_W * CUR_H];
static int cur_x = -1, cur_y = -1;

void gfx_cursor_hide(void)
{
    u16 *fb = gfx_fb();
    int x, y;

    if (cur_x < 0)
        return;
    for (y = 0; y < CUR_H; y++) {
        int py = cur_y + y;

        if ((unsigned)py >= SCREEN_H)
            continue;
        for (x = 0; x < CUR_W; x++) {
            int px = cur_x + x;

            if ((unsigned)px < SCREEN_W)
                fb[py * SCREEN_W + px] = under[y * CUR_W + x];
        }
    }
    cur_x = cur_y = -1;
}

void gfx_cursor_show(int x, int y, u16 fill, u16 edge)
{
    u16 *fb = gfx_fb();
    int i, j;

    gfx_cursor_hide();
    cur_x = x;
    cur_y = y;
    for (j = 0; j < CUR_H; j++) {
        int py = y + j;

        for (i = 0; i < CUR_W; i++) {
            int px = x + i;

            if ((unsigned)px >= SCREEN_W || (unsigned)py >= SCREEN_H) {
                under[j * CUR_W + i] = 0;
                continue;
            }
            under[j * CUR_W + i] = fb[py * SCREEN_W + px];
            if (cursor_bits[j] & (0x8000u >> i))
                fb[py * SCREEN_W + px] = fill;
            else if (cursor_mask[j] & (0x8000u >> i))
                fb[py * SCREEN_W + px] = edge;
        }
    }
}
