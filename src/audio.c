/* audio.c -- sound: a queue of notes, played in the background.
 *
 * The audio interface plays 16-bit stereo samples out of RDRAM by DMA and
 * holds two buffers at once, the one playing and the next.  This keeps
 * three, synthesises a square wave into whichever is free, and is topped up
 * from vi_wait_vblank -- once a frame, whatever else is running -- so a tune
 * queued at the prompt goes on playing while you type, edit or run an app.
 *
 * Nothing touches the hardware until the first note is queued.
 */
#include "n64.h"

#define AI_BASE      0xA4500000u
#define AI_DRAM_ADDR REG(AI_BASE + 0x00)
#define AI_LEN       REG(AI_BASE + 0x04)
#define AI_CONTROL   REG(AI_BASE + 0x08)
#define AI_STATUS    REG(AI_BASE + 0x0C)
#define AI_DACRATE   REG(AI_BASE + 0x10)
#define AI_BITRATE   REG(AI_BASE + 0x14)
#define AI_FULL      0x80000000u

#define VI_CLOCK  48681812u             /* NTSC: the AI counts in these */
#define RATE      22050u
#define CHUNK     512                   /* stereo samples a buffer: 23 ms */
#define NBUF      3
#define NOTES     128
#define RAMP      64                    /* samples of fade, to stop clicks */

static s16 bufs[NBUF][CHUNK * 2] __attribute__((aligned(16)));
static int next_buf;

typedef struct {
    u16 hz;
    u32 length;                         /* in samples */
} note_t;

static note_t queue[NOTES];
static int q_head, q_tail;
static u32 pos;                         /* into the note at q_head */
static u32 phase;
static int amplitude = 6000;            /* of 32767 */
static int started, tail_silence;

static void start(void)
{
    AI_DACRATE = VI_CLOCK / RATE - 1;
    AI_BITRATE = 16 - 1;
    AI_CONTROL = 1;                     /* DMA on */
    started = 1;
}

int audio_note(int hz, int ms)
{
    int next = (q_tail + 1) % NOTES;

    if (ms <= 0)
        return 1;
    if (next == q_head)
        return 0;                       /* full: the caller may wait */
    if (hz < 0)
        hz = 0;
    if (hz > (int)(RATE / 2))
        hz = (int)(RATE / 2);
    queue[q_tail].hz = (u16)hz;
    queue[q_tail].length = (u32)ms * RATE / 1000u;
    q_tail = next;
    if (!started)
        start();
    return 1;
}

void audio_quiet(void)
{
    q_head = q_tail;
    pos = 0;
}

int audio_busy(void)
{
    return q_head != q_tail;
}

void audio_volume(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    amplitude = percent * 240;          /* 100% is 24000 of 32767 */
}

/* One buffer of whatever is queued; silence once it runs out. */
static void fill(s16 *out)
{
    int i;

    for (i = 0; i < CHUNK; i++) {
        s16 v = 0;

        if (q_head != q_tail) {
            note_t *n = &queue[q_head];

            if (n->hz) {
                int a = amplitude;
                u32 left = n->length - pos;

                if (pos < RAMP)
                    a = a * (int)pos / RAMP;
                else if (left < RAMP)
                    a = a * (int)left / RAMP;
                phase += (u32)n->hz * (0xFFFFFFFFu / RATE);
                v = (s16)((phase & 0x80000000u) ? -a : a);
            }
            if (++pos >= n->length) {
                pos = 0;
                q_head = (q_head + 1) % NOTES;
            }
        }
        out[i * 2] = v;
        out[i * 2 + 1] = v;
    }
}

/* Called once a frame.  At most two buffers a call, so a status register
 * that lies cannot make us write over the one that is playing. */
void audio_pump(void)
{
    int n;

    if (!started)
        return;
    if (q_head != q_tail)
        tail_silence = 2;               /* two quiet buffers to end on */
    for (n = 0; n < 2; n++) {
        s16 *b;

        if (q_head == q_tail && tail_silence <= 0)
            return;
        if (AI_STATUS & AI_FULL)
            return;
        if (q_head == q_tail)
            tail_silence--;
        b = (s16 *)UNCACHED(bufs[next_buf]);
        fill(b);
        AI_DRAM_ADDR = PHYS(bufs[next_buf]);
        AI_LEN = CHUNK * 4;
        next_buf = (next_buf + 1) % NBUF;
    }
}
