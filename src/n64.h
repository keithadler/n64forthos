/* Minimal N64 hardware definitions used by the kernel. */
#ifndef N64_H
#define N64_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;

/* KSEG1 (uncached, unmapped) alias of a physical address. */
#define UNCACHED(a) ((void *)(((u32)(a) & 0x1FFFFFFFu) | 0xA0000000u))
#define PHYS(a)     ((u32)(a) & 0x1FFFFFFFu)

#define REG(a) (*(volatile u32 *)(a))

/* Video interface */
#define VI_BASE     0xA4400000u
#define VI_CONTROL  REG(VI_BASE + 0x00)
#define VI_ORIGIN   REG(VI_BASE + 0x04)
#define VI_WIDTH    REG(VI_BASE + 0x08)
#define VI_INTR     REG(VI_BASE + 0x0C)
#define VI_CURRENT  REG(VI_BASE + 0x10)
#define VI_BURST    REG(VI_BASE + 0x14)
#define VI_V_SYNC   REG(VI_BASE + 0x18)
#define VI_H_SYNC   REG(VI_BASE + 0x1C)
#define VI_LEAP     REG(VI_BASE + 0x20)
#define VI_H_START  REG(VI_BASE + 0x24)
#define VI_V_START  REG(VI_BASE + 0x28)
#define VI_V_BURST  REG(VI_BASE + 0x2C)
#define VI_X_SCALE  REG(VI_BASE + 0x30)
#define VI_Y_SCALE  REG(VI_BASE + 0x34)

/* Peripheral interface (cartridge DMA) */
#define PI_BASE       0xA4600000u
#define PI_DRAM_ADDR  REG(PI_BASE + 0x00)
#define PI_CART_ADDR  REG(PI_BASE + 0x04)
#define PI_RD_LEN     REG(PI_BASE + 0x08)
#define PI_WR_LEN     REG(PI_BASE + 0x0C)
#define PI_STATUS     REG(PI_BASE + 0x10)

/* Screen: 640x480 interlaced, 16bpp RGBA5551. */
#define SCREEN_W 640
#define SCREEN_H 480

#define RGB(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 3) << 6) | \
                            (((b) >> 3) << 1) | 1))

/* video.c */
void vi_init(void *framebuffer);
void vi_wait_vblank(void);
u32 vi_frames(void);

/* gfx.c -- everything that touches pixels */
u16 *gfx_fb(void);
void gfx_cls(u16 c);
void gfx_plot(int x, int y, u16 c);
void gfx_box(int x, int y, int w, int h, u16 c);
void gfx_frame(int x, int y, int w, int h, u16 c);
void gfx_hline(int x, int y, int w, u16 c);
void gfx_vline(int x, int y, int h, u16 c);
void gfx_line(int x0, int y0, int x1, int y1, u16 c);
void gfx_glyph(int x, int y, char ch, u16 fg, u16 bg, int opaque);
int gfx_text(int x, int y, const char *s, int len, u16 c);
void gfx_blit(const u16 *src, int x, int y, int w, int h, int keyed);

/* console.c */
void con_init(u16 bg);
void con_color(u16 fg);
u16 con_get_color(void);
void con_clear(void);
void con_putc(char c);
void con_puts(const char *s);
void con_printf(const char *fmt, ...);
void con_status(const char *s);
void con_at(int row, int col);
int con_row(void);
void con_scroll_region(int top, int bottom);

/* kernel.c */
void panic(const char *msg);
u32 rdram_size(void);

/* forth.c */
void forth_init(void);
void forth_eval(const char *src);
void forth_eval_lines(const char *src);
int forth_depth(void);

#endif /* N64_H */
