/* desktop.c -- the shell: a list of applications, and a window for the one
 * you opened.
 *
 * An application here is Forth source held in the cartridge.  Opening its
 * window compiles it and shows you the code; A runs the word it defines,
 * into the canvas the desktop lends it; B closes the window and unloads the
 * app's words again.  There is nothing between the source you are reading
 * and the pixels it draws.
 */
#include "n64.h"
#include "apps/mandel_fth.h"
#include "apps/cornell_fth.h"
#include "apps/navier_fth.h"
#include "apps/life_fth.h"

#define DESK_X   8
#define DESK_Y   32
#define DESK_W   624
#define DESK_H   434

#define SRC_X    16
#define SRC_Y    64
#define SRC_COLS 43
#define SRC_ROWS 24

#define CAN_X    368
#define CAN_Y    64
#define CAN_W    256
#define CAN_H    256

#define RUN_X    (DESK_X + DESK_W - 128)
#define CLOSE_X  (DESK_X + DESK_W - 64)

#define ROW_H 48                /* two lines of text and a line of air */

static int hit(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

#define APP_FORTH   0
#define APP_CONSOLE 1
#define APP_DEVICES 2

typedef struct {
    const char *name;
    const char *blurb;
    const char *source;         /* Forth source, for APP_FORTH */
    const char *entry;          /* the word A runs */
    int kind;
} app_t;

static const app_t apps[] = {
    { "Mandelbrot", "escape-time fractal, 16.16 fixed point",
      mandel_fth, "ROW", APP_FORTH },
    { "Cornell box", "ray tracer: five walls, two spheres, a light",
      cornell_fth, "ROW", APP_FORTH },
    { "Navier-Stokes", "the finite-time blowup, in similarity variables",
      navier_fth, "ROW", APP_FORTH },
    { "Life", "Conway's life, 64 by 64, on a torus",
      life_fth, "ROW", APP_FORTH },
    { "Console", "the Forth prompt, keyboard or controller", 0, 0,
      APP_CONSOLE },
    { "Devices", "what is plugged in, and teaching it the keyboard", 0, 0,
      APP_DEVICES },
};
#define NAPPS ((int)(sizeof(apps) / sizeof(apps[0])))

static u16 c_desk, c_win, c_bar, c_edge, c_text, c_dim, c_amber, c_ink, c_cyan;

static int slen(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (int)(p - s);
}

static void text_at(int x, int y, const char *s, u16 c)
{
    gfx_text(x, y, s, slen(s), c);
}

/* ------------------------------------------------------------ the list */

/* One row of the launcher.  Repainting the whole window for a change of
 * selection costs ten frames of not reading the controller, which is long
 * enough to swallow a button press. */
static void draw_row(int i, int on)
{
    int y = 112 + i * ROW_H;

    gfx_box(56, y, 528, 32, on ? RGB(40, 52, 96) : c_win);
    text_at(72, y, on ? ">" : " ", c_cyan);
    text_at(88, y, apps[i].name, on ? c_amber : c_text);
    text_at(88, y + 16, apps[i].blurb, c_dim);
}

static void draw_desktop(int sel)
{
    int i;

    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(48, 48, 544, 384, c_win);
    gfx_box(48, 48, 544, 20, c_bar);
    gfx_frame(48, 48, 544, 384, c_edge);
    text_at(56, 48, "n64forthos", c_ink);
    text_at(56, 80, "A Forth system with a desktop. Pick something:", c_dim);

    for (i = 0; i < NAPPS; i++)
        draw_row(i, i == sel);
    text_at(56, 400, "d-pad select or point and click     A open", c_dim);
}

/* ------------------------------------------------------- an app's window */

static void draw_source(const char *src, int top)
{
    int row, i;
    const char *p = src;

    for (i = 0; i < top && *p; i++) {
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
    gfx_box(SRC_X - 8, SRC_Y - 2, SRC_COLS * 8 + 12, SRC_ROWS * 16 + 4,
            RGB(14, 18, 40));
    for (row = 0; row < SRC_ROWS; row++) {
        int n = 0;
        char buf[SRC_COLS + 1];
        u16 colour = c_text;

        while (*p && *p != '\n') {
            if (n < SRC_COLS)
                buf[n++] = *p;
            p++;
        }
        if (*p)
            p++;
        buf[n] = 0;
        if (n && buf[0] == '\\')
            colour = c_dim;                 /* comments, in their own colour */
        gfx_text(SRC_X, SRC_Y + row * 16, buf, n, colour);
    }
}

static void draw_app_frame(const app_t *app)
{
    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(DESK_X, DESK_Y, DESK_W, DESK_H, c_win);
    gfx_box(DESK_X, DESK_Y, DESK_W, 20, c_bar);
    gfx_frame(DESK_X, DESK_Y, DESK_W, DESK_H, c_edge);
    text_at(DESK_X + 8, DESK_Y, app->name, c_ink);
    text_at(DESK_X + 232, DESK_Y, "A run   B close   up/down scroll", c_ink);
    /* The same two things, for a pointer. */
    gfx_box(RUN_X, DESK_Y, 56, 18, RGB(30, 38, 74));
    text_at(RUN_X + 16, DESK_Y, "RUN", c_amber);
    gfx_box(CLOSE_X, DESK_Y, 56, 18, RGB(30, 38, 74));
    text_at(CLOSE_X + 8, DESK_Y, "CLOSE", c_text);

    gfx_box(CAN_X, CAN_Y, CAN_W, CAN_H, RGB(8, 10, 30));
    gfx_frame(CAN_X - 2, CAN_Y - 2, CAN_W + 4, CAN_H + 4, c_edge);
}

/* Kept on the character grid, so the test harness can read it back. */
#define STATUS_Y (CAN_Y + CAN_H + 16)

static void app_status(const char *s, u16 colour)
{
    gfx_box(CAN_X, STATUS_Y, CAN_W, 16, c_win);
    text_at(CAN_X, STATUS_Y, s, colour);
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static void put_u32(char *p, u32 v)
{
    char tmp[12];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n--)
        *p++ = tmp[n];
    *p = 0;
}

static void open_app(const app_t *app)
{
    u32 mark = forth_mark();
    int top = 0, lines = 0;
    int running = 0, row = 0, rows = 0, passes = 0;
    u32 started = 0;
    const char *p;

    for (p = app->source; *p; p++)
        if (*p == '\n')
            lines++;

    draw_app_frame(app);
    app_status("compiling...", c_dim);
    forth_set_canvas(CAN_X, CAN_Y, CAN_W, CAN_H);
    forth_eval_lines(app->source);
    draw_source(app->source, top);
    app_status("A runs it", c_dim);

    for (;;) {
        u16 pressed;
        const mouse_t *ms;
        int clicked_run = 0;

        input_poll();
        pressed = input_pressed(0);
        ms = input_mouse();
        if (ms->present) {
            if (ms->edges & MOUSE_LEFT) {
                if (hit(ms->x, ms->y, RUN_X, DESK_Y, 56, 18))
                    clicked_run = 1;
                if (hit(ms->x, ms->y, CLOSE_X, DESK_Y, 56, 18))
                    pressed |= PAD_B;
            }
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        }

        if ((pressed & PAD_B) && running) {
            running = 0;                /* stop the picture, keep the window */
            app_status("stopped", c_dim);
            continue;
        }
        if (pressed & PAD_B) {
            gfx_cursor_hide();
            forth_release(mark);
            forth_set_canvas(0, 0, SCREEN_W, SCREEN_H);
            return;
        }
        if (pressed & (PAD_UP | PAD_DOWN)) {
            top += (pressed & PAD_DOWN) ? SRC_ROWS / 2 : -SRC_ROWS / 2;
            if (top > lines - 4)
                top = lines - 4;
            if (top < 0)
                top = 0;
            draw_source(app->source, top);
        }
        if ((pressed & (PAD_A | PAD_START)) || clicked_run) {
            /* Start it.  The picture is drawn a row per frame so that the
             * controller still answers while it paints -- and so that B can
             * stop it half way. */
            gfx_cursor_hide();
            gfx_box(CAN_X, CAN_Y, CAN_W, CAN_H, RGB(8, 10, 30));
            rows = forth_call("ROWS") ? forth_pop() : 0;
            row = 0;
            passes = 0;
            forth_call("START");        /* optional: reset the app's state */
            started = vi_frames();
            running = (rows > 0);
            if (!running)
                app_status("this app defines no ROWS", c_amber);
        }

        if (running) {
            char msg[48];
            char *m = msg;
            u32 line = vi_line();
            int did = 0;

            /* As many rows as fit in this frame.  A compiled application can
             * paint several while the video interface is still on the same
             * field, and there is no sense idling through a vertical blank
             * with work in hand -- but stop at the frame boundary so the
             * controller still gets read. */
            do {
                forth_push(row);
                if (!forth_call(app->entry)) {
                    running = 0;
                    app_status("stopped: see the console", c_amber);
                    break;
                }
                row++;
                did++;
            } while (running && row < rows && did < 8 && vi_line() >= line);

            if (!running) {
                /* it stopped itself */
            } else if (row >= rows) {
                /* An app that defines NEXT is an animation: advance its
                 * state and go round again until B stops it. */
                if (forth_call("NEXT")) {
                    row = 0;
                    passes++;
                    m = put_str(m, "pass ");
                    put_u32(m, (u32)passes);
                    m += slen(m);
                    m = put_str(m, "   B stops it");
                    *m = 0;
                    app_status(msg, c_amber);
                } else {
                    running = 0;
                    m = put_str(m, "drawn in ");
                    put_u32(m, vi_frames() - started);
                    m += slen(m);
                    m = put_str(m, " frames");
                    *m = 0;
                    app_status(msg, c_cyan);
                }
            } else if ((row - did) / 16 != row / 16) {
                m = put_str(m, "row ");
                put_u32(m, (u32)row);
                m += slen(m);
                m = put_str(m, " of ");
                put_u32(m, (u32)rows);
                m += slen(m);
                m = put_str(m, "   B stops it");
                *m = 0;
                app_status(msg, c_amber);
            }
        }

        kernel_status_bar();
        vi_wait_vblank();
    }
}

/* ----------------------------------------------------------- the devices
 *
 * With a BlueRetro adapter the four channels can hold a controller, a mouse
 * or a keyboard.  This shows what answered, live, and teaches the keyboard
 * table: the Randnet key codes are not ASCII, and this is how you find out
 * what they are without a logic analyser.
 */
static const char *kind_name(int k)
{
    switch (k) {
    case DEV_PAD:      return "controller";
    case DEV_MOUSE:    return "mouse";
    case DEV_KEYBOARD: return "keyboard";
    default:           return "-";
    }
}

static const char learn_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?+-*/@'\"()[]<>=#$%^&_\\\n";

static char *put_hex(char *p, u32 v)
{
    int i;

    for (i = 3; i >= 0; i--) {
        int d = (v >> (i * 4)) & 15;

        *p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    *p = 0;
    return p;
}

static void devices_app(void)
{
    int learn = -1;                     /* index into learn_chars, -1 = off */
    u16 last_seen = 0;
    int ch;

    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(DESK_X, DESK_Y, DESK_W, DESK_H, c_win);
    gfx_box(DESK_X, DESK_Y, DESK_W, 20, c_bar);
    gfx_frame(DESK_X, DESK_Y, DESK_W, DESK_H, c_edge);
    text_at(DESK_X + 8, DESK_Y, "Devices", c_ink);
    text_at(DESK_X + 232, DESK_Y, "A teach the keyboard    B close", c_ink);

    for (;;) {
        char line[96];
        char *p;
        const mouse_t *ms = input_mouse();
        const kbd_t *kb = input_keyboard();
        u16 pressed;

        input_poll();
        pressed = input_pressed(0);
        if (pressed & PAD_B) {
            gfx_cursor_hide();
            return;
        }
        if ((pressed & PAD_A) && learn < 0) {
            learn = 0;
            con_clear();
        }

        gfx_cursor_hide();
        for (ch = 0; ch < 4; ch++) {
            p = line;
            p = put_str(p, "channel ");
            *p++ = (char)('1' + ch);
            p = put_str(p, "   ");
            p = put_str(p, kind_name(input_kind(ch)));
            p = put_str(p, "                 ");
            *p = 0;
            gfx_box(DESK_X + 16, 72 + ch * 16, 320, 16, c_win);
            text_at(DESK_X + 16, 72 + ch * 16, line, c_text);
        }

        p = line;
        p = put_str(p, "mouse    ");
        if (ms->present) {
            put_u32(p, (u32)ms->x);
            p += slen(p);
            p = put_str(p, ", ");
            put_u32(p, (u32)ms->y);
            p += slen(p);
            p = put_str(p, (ms->buttons & MOUSE_LEFT) ? "  left" : "      ");
            p = put_str(p, (ms->buttons & MOUSE_RIGHT) ? "  right" : "       ");
        } else {
            p = put_str(p, "not present     ");
        }
        *p = 0;
        gfx_box(DESK_X + 16, 152, 400, 16, c_win);
        text_at(DESK_X + 16, 152, line, c_text);

        p = line;
        p = put_str(p, "keyboard ");
        if (kb->present) {
            p = put_str(p, "raw ");
            p = put_hex(p, kb->last_raw);
            p = put_str(p, "   maps to ");
            {
                int a = input_key_mapped(kb->last_raw);

                if (a >= 32 && a < 127) {
                    *p++ = '\'';
                    *p++ = (char)a;
                    *p++ = '\'';
                } else {
                    p = put_str(p, "nothing");
                }
            }
            p = put_str(p, "        ");
        } else {
            p = put_str(p, "not present            ");
        }
        *p = 0;
        gfx_box(DESK_X + 16, 176, 400, 16, c_win);
        text_at(DESK_X + 16, 176, line, c_text);

        if (learn >= 0 && kb->present) {
            if (kb->last_raw && kb->last_raw != last_seen) {
                input_key_map(kb->last_raw, learn_chars[learn]);
                con_printf("%u %u KEY!   ( %c )\n", (u32)kb->last_raw,
                           (u32)(u8)learn_chars[learn],
                           learn_chars[learn] == '\n' ? 'R' : learn_chars[learn]);
                last_seen = kb->last_raw;
                learn++;
                if (!learn_chars[learn]) {
                    learn = -1;
                    con_puts("keyboard learned; the lines above are Forth\n");
                }
            }
            p = line;
            p = put_str(p, "press the key for:  ");
            if (learn >= 0) {
                *p++ = learn_chars[learn] == '\n' ? '<' : learn_chars[learn];
                if (learn_chars[learn] == '\n')
                    p = put_str(p, "return>");
            } else {
                p = put_str(p, "done");
            }
            p = put_str(p, "     ");
            *p = 0;
            gfx_box(DESK_X + 16, 208, 400, 16, c_win);
            text_at(DESK_X + 16, 208, line, c_amber);
        } else if (learn >= 0) {
            gfx_box(DESK_X + 16, 208, 400, 16, c_win);
            text_at(DESK_X + 16, 208, "no keyboard on any channel", c_amber);
        }

        if (ms->present)
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        kernel_status_bar();
        vi_wait_vblank();
    }
}

/* ------------------------------------------------------------ the shell */

void desktop_run(void)
{
    int sel = 0;

    c_desk  = RGB(16, 20, 44);
    c_win   = RGB(26, 32, 64);
    c_bar   = RGB(96, 224, 255);
    c_edge  = RGB(70, 86, 130);
    c_text  = RGB(205, 213, 228);
    c_dim   = RGB(120, 134, 165);
    c_amber = RGB(255, 190, 90);
    c_ink   = RGB(10, 14, 30);
    c_cyan  = RGB(96, 224, 255);

    input_init();
    draw_desktop(sel);

    for (;;) {
        u16 pressed;
        const mouse_t *ms;

        input_poll();
        pressed = input_pressed(0);
        ms = input_mouse();
        if (ms->present) {
            if (ms->edges & MOUSE_LEFT) {
                int i;

                for (i = 0; i < NAPPS; i++)
                    if (hit(ms->x, ms->y, 56, 112 + i * ROW_H, 528, 32)) {
                        gfx_cursor_hide();
                        draw_row(sel, 0);
                        sel = i;
                        draw_row(sel, 1);
                        pressed |= PAD_A;
                    }
            }
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        }

        if (pressed & (PAD_DOWN | PAD_UP)) {
            int was = sel;

            gfx_cursor_hide();
            sel = (pressed & PAD_DOWN) ? (sel + 1) % NAPPS
                                       : (sel + NAPPS - 1) % NAPPS;
            draw_row(was, 0);
            draw_row(sel, 1);
        }
        if (pressed & (PAD_A | PAD_START)) {
            gfx_cursor_hide();
            if (apps[sel].kind == APP_FORTH) {
                open_app(&apps[sel]);
            } else if (apps[sel].kind == APP_DEVICES) {
                devices_app();
            } else {
                /* The console owns its own columns; the desktop behind it
                 * has to be painted out before handing over. */
                gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, RGB(10, 14, 30));
                con_clear();
                repl_run();             /* returns when L is pressed */
            }
            draw_desktop(sel);
        }

        kernel_status_bar();
        vi_wait_vblank();
    }
}
