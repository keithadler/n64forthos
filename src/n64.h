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

/* input.c -- controllers */
#define PAD_A      0x8000
#define PAD_B      0x4000
#define PAD_Z      0x2000
#define PAD_START  0x1000
#define PAD_UP     0x0800
#define PAD_DOWN   0x0400
#define PAD_LEFT   0x0200
#define PAD_RIGHT  0x0100
#define PAD_L      0x0020
#define PAD_R      0x0010
#define PAD_CUP    0x0008
#define PAD_CDOWN  0x0004
#define PAD_CLEFT  0x0002
#define PAD_CRIGHT 0x0001

#define DEV_NONE     0
#define DEV_PAD      1
#define DEV_MOUSE    2
#define DEV_KEYBOARD 3

/* The mouse reports its two buttons where a controller reports A and B. */
#define MOUSE_LEFT   PAD_A
#define MOUSE_RIGHT  PAD_B

typedef struct {
    u16 buttons;
    s8 stick_x, stick_y;
    u8 present;
} pad_t;

typedef struct {
    int x, y;                   /* the kernel keeps the pointer position */
    u16 buttons, edges;
    u8 present;
} mouse_t;

typedef struct {
    u16 keys[3];                /* the keyboard reports up to three at once */
    u16 last_raw;
    u8 nkeys;
    u8 present;
} kbd_t;

void input_init(void);
void input_poll(void);
const pad_t *input_pad(int n);
const mouse_t *input_mouse(void);
const kbd_t *input_keyboard(void);
int input_kind(int n);
int input_getchar(void);
void input_key_map(int code, int ascii);
int input_key_mapped(int code);
u16 input_buttons(int n);
u16 input_pressed(int n);

/* repl.c -- the prompt, driven by the on-screen keyboard */
void repl_run(void);

/* rdp.c -- rectangle fills, done by the hardware that is good at them */
void rdp_init(void *framebuffer);
void rdp_fill(int x, int y, int w, int h, u16 colour);
void rdp_wait(void);
int rdp_ready(void);

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
void gfx_cursor_show(int x, int y, u16 fill, u16 edge);
void gfx_cursor_hide(void);

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
void con_erase_row(int row);
int con_col(void);

/* kernel.c */
void panic(const char *msg);
void kernel_status_bar(void);
u32 rdram_size(void);

/* forth.c */
void forth_init(void);
void forth_eval(const char *src);
void forth_eval_lines(const char *src);
void forth_set_canvas(int x, int y, int w, int h);
void forth_push(s32 v);
s32 forth_pop(void);
int forth_call(const char *name);
u32 forth_mark(void);
u32 forth_here(void);
u32 forth_abort_flag(void);
u32 forth_rstack_base(void);
u32 forth_rsp_addr(void);
u32 forth_stack_base(void);
u32 forth_stack_top(void);
u32 forth_limit(void);
void forth_set_here(u32 where);
int native_compile(s32 xt);
u32 native_compiled(void);
u32 native_refused(void);
void forth_release(u32 mark);
u32 forth_word_count(void);

/* desktop.c */
void desktop_run(void);
int forth_depth(void);

#endif /* N64_H */
