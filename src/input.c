/* input.c -- controllers, mice and keyboards, over the serial interface.
 *
 * The CPU never talks to a peripheral directly.  It writes a block of joybus
 * commands into PIF RAM by DMA, the PIF walks the four channels and writes
 * the replies back into the same block, and the CPU DMAs it home.
 *
 * Three kinds of device are understood, which is what a BlueRetro adapter
 * can present over Bluetooth: the standard controller, the N64 mouse (which
 * reports relative movement where a controller reports its stick), and the
 * Randnet keyboard.  Devices are identified once a second so that plugging
 * something in mid-session is noticed.
 *
 * Caveat worth reading before trusting the keyboard: the mouse and
 * controller protocols here are the well-worn ones, but the Randnet key
 * codes are from documentation and have never been checked against the real
 * keyboard.  The Keyboard app on the desktop shows raw codes for exactly
 * that reason, and KEY! lets Forth correct the table at runtime.
 */
#include "n64.h"

#define SI_BASE           0xA4800000u
#define SI_DRAM_ADDR      REG(SI_BASE + 0x00)
#define SI_PIF_ADDR_RD64B REG(SI_BASE + 0x04)
#define SI_PIF_ADDR_WR64B REG(SI_BASE + 0x10)
#define SI_STATUS         REG(SI_BASE + 0x18)
#define SI_BUSY           0x3u
#define PIF_RAM           0x1FC007C0u

#define CMD_INFO   0x00
#define CMD_STATE  0x01
#define CMD_KEYS   0x13

static u8 block[64] __attribute__((aligned(16)));
static u8 kind[4];
static u8 accessory[4];         /* the third identify byte: 1 = pak inserted */
static pad_t pads[4];
static u16 last_buttons[4];
static u16 edges[4];
static mouse_t mouse;
static kbd_t kbd;
static u16 kbd_last[3];

/* Keys are caught as they go down, at every poll, and queued: whoever asks
 * for a character later still gets it, even if the key was pressed while a
 * window was opening or a picture was being drawn. */
#define TYPEAHEAD 64
static u8 typed[TYPEAHEAD];
static int typed_head, typed_tail;
static int identify_countdown;

/* Scancode to ASCII.  The default is the identity, which is what our own
 * emulator sends: it types ASCII down the wire.  A real Randnet keyboard
 * sends its own codes, so the Devices app teaches the table and KEY! sets
 * entries from Forth. */
static u8 keymap[256];

static void keymap_default(void)
{
    int c;

    for (c = 0; c < 256; c++)
        keymap[c] = 0;
    for (c = 0x20; c <= 0x7E; c++)
        keymap[c] = (u8)c;
    for (c = 0x01; c < 0x20; c++)
        keymap[c] = (u8)c;              /* control keys: ^S, ^Q, escape */
    for (c = KEY_UP; c <= KEY_DEL; c++)
        keymap[c] = (u8)c;              /* the editing keys, see n64.h */
    keymap[0x08] = '\b';
    keymap[0x0D] = '\n';
    keymap[0x0A] = '\n';
}

void input_key_map(int code, int ascii)
{
    if (code >= 0 && code < 256)
        keymap[code] = (u8)ascii;
}

int input_key_mapped(int code)
{
    return (code >= 0 && code < 256) ? keymap[code] : 0;
}

static void si_wait(void)
{
    u32 guard;

    for (guard = 0; guard < 100000u; guard++)
        if (!(SI_STATUS & SI_BUSY))
            return;
}

void input_exchange(void)
{
    si_wait();
    SI_DRAM_ADDR = PHYS(block);
    SI_PIF_ADDR_WR64B = PIF_RAM;        /* block -> PIF, and it runs */
    si_wait();
    SI_DRAM_ADDR = PHYS(block);
    SI_PIF_ADDR_RD64B = PIF_RAM;        /* PIF -> block, replies inside */
    si_wait();
    SI_STATUS = 0;
}

static u8 *b(void)
{
    return (u8 *)UNCACHED(block);
}

/* The command block, for pak.c, which speaks to the accessory slot. */
u8 *input_block(void)
{
    return b();
}

static void clear_block(void)
{
    int i;

    for (i = 0; i < 64; i++)
        b()[i] = 0;
}

/* Ask every channel what it is. */
static void identify(void)
{
    u8 *p = b();
    int ch, at = 0, off[4];

    clear_block();
    for (ch = 0; ch < 4; ch++) {
        p[at++] = 0xFF;                 /* dummy, keeps things word aligned */
        p[at++] = 1;                    /* one byte out */
        p[at++] = 3;                    /* three back */
        p[at++] = CMD_INFO;
        off[ch] = at;
        p[at++] = 0xFF;
        p[at++] = 0xFF;
        p[at++] = 0xFF;
        p[at++] = 0xFF;                 /* pad the group to eight bytes */
    }
    p[at++] = 0xFE;
    p[63] = 1;
    input_exchange();

    for (ch = 0; ch < 4; ch++) {
        u8 rx = p[ch * 8 + 2];
        u16 type = (u16)((p[off[ch]] << 8) | p[off[ch] + 1]);

        if (rx & 0xC0) {
            kind[ch] = DEV_NONE;
            accessory[ch] = 0;
            continue;
        }
        accessory[ch] = p[off[ch] + 2];
        switch (type) {
        case 0x0500: kind[ch] = DEV_PAD; break;
        case 0x0200: kind[ch] = DEV_MOUSE; break;
        case 0x0002: kind[ch] = DEV_KEYBOARD; break;
        default:     kind[ch] = type ? DEV_PAD : DEV_NONE; break;
        }
    }
}

void input_init(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        kind[i] = DEV_NONE;
        pads[i].present = 0;
        pads[i].buttons = 0;
        last_buttons[i] = 0;
        edges[i] = 0;
    }
    mouse.present = 0;
    mouse.x = SCREEN_W / 2;
    mouse.y = SCREEN_H / 2;
    mouse.buttons = 0;
    mouse.edges = 0;
    kbd.present = 0;
    kbd.nkeys = 0;
    kbd.last_raw = 0;
    {   /* Once: a table taught with KEY! outlives the window it was taught in. */
        static int keymap_ready;

        if (!keymap_ready)
            keymap_default();
        keymap_ready = 1;
    }
    identify_countdown = 0;
    identify();
    /* Whatever is held right now was pressed to get here -- to open this
     * window, most likely -- so it is not a fresh press.  Without this the
     * console types its highlighted key the moment it opens. */
    input_poll();
    for (i = 0; i < 4; i++)
        edges[i] = 0;
    mouse.edges = 0;
}

void input_poll(void)
{
    u8 *p = b();
    int ch, at = 0, off[4], len[4];

    if (--identify_countdown <= 0) {
        identify();
        identify_countdown = 60;        /* once a second */
    }

    clear_block();
    for (ch = 0; ch < 4; ch++) {
        if (kind[ch] == DEV_NONE) {
            off[ch] = -1;
            len[ch] = 0;
            p[at++] = 0x00;             /* skip this channel */
            continue;
        }
        p[at++] = 0xFF;
        if (kind[ch] == DEV_KEYBOARD) {
            p[at++] = 2;                /* command plus one byte of state */
            p[at++] = 7;
            p[at++] = CMD_KEYS;
            p[at++] = 0x00;
            off[ch] = at;
            len[ch] = 7;
        } else {
            p[at++] = 1;
            p[at++] = 4;
            p[at++] = CMD_STATE;
            off[ch] = at;
            len[ch] = 4;
        }
        at += len[ch];
    }
    p[at++] = 0xFE;
    p[63] = 1;
    input_exchange();

    mouse.edges = 0;
    kbd.nkeys = 0;

    for (ch = 0; ch < 4; ch++) {
        u8 *r;

        if (off[ch] < 0)
            continue;
        r = p + off[ch];

        if (kind[ch] == DEV_KEYBOARD) {
            int i, j, n = 0;

            kbd.present = 1;
            for (i = 0; i < 3; i++) {
                u16 code = (u16)((r[i * 2] << 8) | r[i * 2 + 1]);

                if (code) {
                    kbd.keys[n++] = code;
                    kbd.last_raw = code;
                }
            }
            kbd.nkeys = (u8)n;
            for (i = 0; i < n; i++) {
                int fresh = 1, c;

                for (j = 0; j < 3; j++)
                    if (kbd_last[j] == kbd.keys[i])
                        fresh = 0;
                c = input_key_mapped(kbd.keys[i]);
                if (fresh && c && ((typed_tail + 1) % TYPEAHEAD) != typed_head) {
                    typed[typed_tail] = (u8)c;
                    typed_tail = (typed_tail + 1) % TYPEAHEAD;
                }
            }
            for (j = 0; j < 3; j++)
                kbd_last[j] = (j < n) ? kbd.keys[j] : 0;
            continue;
        }
        if (kind[ch] == DEV_MOUSE) {
            u16 now = (u16)((r[0] << 8) | r[1]);

            mouse.present = 1;
            mouse.edges = (u16)(now & ~mouse.buttons);
            mouse.buttons = now;
            mouse.x += (s8)r[2];
            mouse.y -= (s8)r[3];        /* the mouse reports up as positive */
            if (mouse.x < 0) mouse.x = 0;
            if (mouse.y < 0) mouse.y = 0;
            if (mouse.x > SCREEN_W - 1) mouse.x = SCREEN_W - 1;
            if (mouse.y > SCREEN_H - 1) mouse.y = SCREEN_H - 1;
            continue;
        }
        {
            u16 now = (u16)((r[0] << 8) | r[1]);

            pads[ch].present = 1;
            pads[ch].buttons = now;
            pads[ch].stick_x = (s8)r[2];
            pads[ch].stick_y = (s8)r[3];
            edges[ch] = (u16)(now & ~last_buttons[ch]);
            last_buttons[ch] = now;
        }
    }
    for (ch = 0; ch < 4; ch++) {
        if (kind[ch] != DEV_PAD) {
            pads[ch].present = 0;
            pads[ch].buttons = 0;
            edges[ch] = 0;
            last_buttons[ch] = 0;
        }
    }
}

/* Is someone asking a running program to stop?  Esc or ^C on a keyboard,
 * or START and Z held together on the controller.  Called from inside
 * running Forth, so it reads only what it may: keyboards (whose other keys
 * go into the typeahead as usual) and controllers, never a mouse, whose
 * movement is consumed by reading it, and without touching the edges the
 * next input_poll() will report. */
int input_break_check(void)
{
    u8 *p = b();
    int ch, at = 0, off[4], hit = 0;

    clear_block();
    for (ch = 0; ch < 4; ch++) {
        off[ch] = -1;
        if (kind[ch] == DEV_KEYBOARD) {
            p[at++] = 0xFF;
            p[at++] = 2;
            p[at++] = 7;
            p[at++] = CMD_KEYS;
            p[at++] = 0x00;
            off[ch] = at;
            at += 7;
        } else if (kind[ch] == DEV_PAD) {
            p[at++] = 0xFF;
            p[at++] = 1;
            p[at++] = 4;
            p[at++] = CMD_STATE;
            off[ch] = at;
            at += 4;
        } else {
            p[at++] = 0x00;
        }
    }
    p[at++] = 0xFE;
    p[63] = 1;
    input_exchange();

    for (ch = 0; ch < 4; ch++) {
        u8 *r;

        if (off[ch] < 0)
            continue;
        r = p + off[ch];
        if (kind[ch] == DEV_PAD) {
            u16 now = (u16)((r[0] << 8) | r[1]);

            if ((now & (PAD_START | PAD_Z)) == (PAD_START | PAD_Z))
                hit = 1;
            continue;
        }
        {
            u16 keys[3];
            int i, j, n = 0;

            for (i = 0; i < 3; i++) {
                u16 code = (u16)((r[i * 2] << 8) | r[i * 2 + 1]);

                if (code)
                    keys[n++] = code;
            }
            for (i = 0; i < n; i++) {
                int fresh = 1, c;

                for (j = 0; j < 3; j++)
                    if (kbd_last[j] == keys[i])
                        fresh = 0;
                c = input_key_mapped(keys[i]);
                if (!fresh || !c)
                    continue;
                if (c == KEY_ESC || c == CTRL('C'))
                    hit = 1;
                else if (((typed_tail + 1) % TYPEAHEAD) != typed_head) {
                    typed[typed_tail] = (u8)c;
                    typed_tail = (typed_tail + 1) % TYPEAHEAD;
                }
            }
            for (j = 0; j < 3; j++)
                kbd_last[j] = (j < n) ? keys[j] : 0;
        }
    }
    return hit;
}

/* The next key that went down, translated where we can; 0 if none. */
int input_getchar(void)
{
    int c;

    if (typed_head == typed_tail)
        return 0;
    c = typed[typed_head];
    typed_head = (typed_head + 1) % TYPEAHEAD;
    return c;
}

const pad_t *input_pad(int n) { return &pads[n & 3]; }
const mouse_t *input_mouse(void) { return &mouse; }
const kbd_t *input_keyboard(void) { return &kbd; }
int input_kind(int n) { return kind[n & 3]; }
int input_accessory(int n) { return kind[n & 3] == DEV_PAD && (accessory[n & 3] & 1); }
u16 input_buttons(int n) { return pads[n & 3].buttons; }
u16 input_pressed(int n) { return edges[n & 3]; }
