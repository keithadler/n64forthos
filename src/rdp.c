/* rdp.c -- the Reality Display Processor, for the one thing we ask of it:
 * filling rectangles.
 *
 * The CPU fills a rectangle by writing every pixel itself, uncached, one
 * halfword at a time; the RDP writes four pixels a cycle at 62.5 MHz and the
 * CPU only has to hand it a list.  A full-screen clear is the difference
 * between hundreds of thousands of stalled writes and a dozen words of
 * command list.
 *
 * The list is built in RDRAM through the uncached alias, so there is nothing
 * to flush before the RDP reads it.
 */
#include "n64.h"

#define DPC_BASE    0xA4100000u
#define DPC_START   REG(DPC_BASE + 0x00)
#define DPC_END     REG(DPC_BASE + 0x04)
#define DPC_CURRENT REG(DPC_BASE + 0x08)
#define DPC_STATUS  REG(DPC_BASE + 0x0C)

/* status: written to clear, read to see what it is doing */
#define DPC_CLEAR_XBUS   0x001
#define DPC_CLEAR_FREEZE 0x004
#define DPC_CLEAR_FLUSH  0x010
#define DPC_START_GCLK   0x008
#define DPC_PIPE_BUSY    0x020
#define DPC_CMD_BUSY     0x040

/* commands, one 64-bit word each */
#define CMD_FILL_RECT      0xF6u
#define CMD_SET_FILL_COLOR 0xF7u
#define CMD_SET_SCISSOR    0xEDu
#define CMD_SET_OTHER      0xEFu
#define CMD_SET_COLOR_IMG  0xFFu
#define CMD_SYNC_FULL      0xE9u
#define CMD_SYNC_PIPE      0xE7u

#define LIST_WORDS 64

static u32 list[LIST_WORDS] __attribute__((aligned(16)));
static int used;
static int ready;

static volatile u32 *dl(void)
{
    return (volatile u32 *)UNCACHED(list);
}

static void put(u32 hi, u32 lo)
{
    if (used + 2 > LIST_WORDS)
        return;
    dl()[used++] = hi;
    dl()[used++] = lo;
}

void rdp_wait(void)
{
    u32 guard;

    for (guard = 0; guard < 2000000u; guard++)
        if (!(DPC_STATUS & (DPC_PIPE_BUSY | DPC_CMD_BUSY | DPC_START_GCLK)))
            return;
}

static void run(void)
{
    if (!used)
        return;
    DPC_START = PHYS(list);
    DPC_END = PHYS(list) + used * 4;
    rdp_wait();
    used = 0;
}

/* Point it at the framebuffer and put it in fill mode.  Done once. */
void rdp_init(void *framebuffer)
{
    DPC_STATUS = DPC_CLEAR_XBUS | DPC_CLEAR_FREEZE | DPC_CLEAR_FLUSH;
    used = 0;
    put((CMD_SET_COLOR_IMG << 24) | (0 << 21) | (2 << 19) | (SCREEN_W - 1),
        PHYS(framebuffer));                     /* RGBA, 16 bits, 640 across */
    put((CMD_SET_SCISSOR << 24) | 0,
        ((SCREEN_W << 2) << 12) | (SCREEN_H << 2));
    put(0xEFB000FFu, 0x00004000u);              /* other modes: fill cycle */
    put(CMD_SYNC_FULL << 24, 0);
    run();
    ready = 1;
}

int rdp_ready(void)
{
    return ready;
}

/* The rectangle is given in pixels; the RDP wants 10.2 fixed point and an
 * inclusive lower-right corner. */
void rdp_fill(int x, int y, int w, int h, u16 colour)
{
    int x1, y1;

    if (!ready || w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;
    x1 = x + w - 1;
    y1 = y + h - 1;

    put(CMD_SET_FILL_COLOR << 24, ((u32)colour << 16) | colour);
    put((CMD_FILL_RECT << 24) | (x1 << 14) | (y1 << 2), (x << 14) | (y << 2));
    put(CMD_SYNC_FULL << 24, 0);
    run();
}
