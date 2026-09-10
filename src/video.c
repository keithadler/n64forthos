/* Video interface: 640x480, 16-bit RGBA5551, interlaced NTSC. */
#include "n64.h"

static u32 frame_count;

void vi_init(void *framebuffer)
{
    VI_CONTROL = 0;                 /* blank while the timing is reprogrammed */
    VI_ORIGIN  = PHYS(framebuffer);
    VI_WIDTH   = SCREEN_W;
    VI_INTR    = 2;
    VI_BURST   = 0x03E52239;        /* NTSC colour burst */
    VI_V_SYNC  = 0x0000020D;        /* 525 half-lines */
    VI_H_SYNC  = 0x00000C15;
    VI_LEAP    = 0x0C150C15;
    VI_H_START = 0x006C02EC;        /* active 108..748 -> 640 px */
    VI_V_START = 0x002501FF;        /* active 37..511  -> 480 lines */
    VI_V_BURST = 0x000E0204;
    VI_X_SCALE = 0x00000400;        /* 1.0: 640 source pixels across */
    VI_Y_SCALE = 0x00000400;        /* 1.0: 480 lines over two fields */
    /* 16bpp | serrate (interlace) | no AA | pixel advance 3 */
    VI_CONTROL = 0x00003342;
}

/* Wait for the start of vertical blank.  The spin caps keep a mis-modelled
 * VI_CURRENT (some emulators) from wedging the kernel. */
void vi_wait_vblank(void)
{
    u32 guard;

    for (guard = 0; guard < 2000000u; guard++)
        if ((VI_CURRENT >> 1) < SCREEN_H)
            break;
    for (guard = 0; guard < 2000000u; guard++)
        if ((VI_CURRENT >> 1) >= SCREEN_H)
            break;
    frame_count++;
}

/* Which line the video interface is on, right now.  vi_frames() only moves
 * when something waits for a blank, so anything that wants to know how much
 * of a frame it has used has to ask the hardware. */
u32 vi_line(void)
{
    return VI_CURRENT >> 1;
}

u32 vi_frames(void)
{
    return frame_count;
}
