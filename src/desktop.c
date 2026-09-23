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

#define ROW_H 32                /* two lines of text, on the character grid */
#define LIST_TOP 96

/* The controller's buttons, with Enter and Escape on a keyboard standing
 * in for A and B, so a keyboard alone can drive the desktop. */
static u16 pressed_now(void)
{
    u16 pressed = input_pressed(0);
    int c = input_getchar();

    if (c == '\n')
        pressed |= PAD_A;
    if (c == KEY_ESC)
        pressed |= PAD_B;
    if (c == KEY_UP)
        pressed |= PAD_UP;
    if (c == KEY_DOWN)
        pressed |= PAD_DOWN;
    return pressed;
}

static int hit(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

#define APP_FORTH   0
#define APP_CONSOLE 1
#define APP_DEVICES 2
#define APP_FULL    3   /* the application owns the whole screen */
#define APP_FILES   4

/* An application is a file.  The desktop opens it by name, so a copy of
 * MANDEL.FTH saved on the Controller Pak is the Mandelbrot you get. */
typedef struct {
    const char *name;
    const char *blurb;
    const char *file;           /* Forth source, for APP_FORTH and APP_FULL */
    int kind;
} app_t;

static const app_t apps[] = {
    { "Files", "your programs and notes: edit, run, keep on the Pak", 0,
      APP_FILES },
    { "Console", "the Forth prompt, keyboard or controller", 0,
      APP_CONSOLE },
    { "Mandelbrot", "escape-time fractal, 16.16 fixed point",
      "MANDEL.FTH", APP_FORTH },
    { "Cornell box", "ray tracer: five walls, two spheres, a light",
      "CORNELL.FTH", APP_FORTH },
    { "Navier-Stokes", "the finite-time blowup, in similarity variables",
      "NAVIER.FTH", APP_FORTH },
    { "Life", "Conway's life, 64 by 64, on a torus",
      "LIFE.FTH", APP_FORTH },
    { "Windows", "a window manager, written in Forth", "WM.FTH",
      APP_FULL },
    { "Tasks", "Mandelbrot, Life and Navier-Stokes at once, in windows",
      "TASKS.FTH", APP_FULL },
    { "Devices", "what is plugged in, and teaching it the keyboard", 0,
      APP_DEVICES },
};

/* The source of whichever application is open. */
static char app_src[FS_FILE_MAX + 1];

static int load_app(const char *file)
{
    int n = 0;

    while (file[n])
        n++;
    n = fs_read(file, n, app_src, FS_FILE_MAX);
    if (n < 0) {
        app_src[0] = 0;
        return n;
    }
    app_src[n] = 0;
    return 0;
}
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
    int y = LIST_TOP + i * ROW_H;

    gfx_box(56, y, 528, 32, on ? RGB(40, 52, 96) : c_win);
    text_at(72, y, on ? ">" : " ", c_cyan);
    text_at(88, y, apps[i].name, on ? c_amber : c_text);
    text_at(88, y + 16, apps[i].blurb, c_dim);
}

static char *put_str(char *p, const char *s);
static void put_u32(char *p, u32 v);

/* Where your files are going, at the foot of the launcher. */
static void draw_storage(void)
{
    char line[64], *p = line;

    switch (fs_state()) {
    case FS_PAK:
        p = put_str(p, "Controller Pak: ");
        put_u32(p, (u32)fs_pak_files());
        p += slen(p);
        p = put_str(p, fs_pak_files() == 1 ? " file, " : " files, ");
        put_u32(p, (u32)fs_free_bytes());
        p += slen(p);
        p = put_str(p, " bytes free");
        break;
    case FS_RAM:
        p = put_str(p, "no Controller Pak: files kept in RAM until power off");
        break;
    case FS_UNFORMATTED:
        p = put_str(p, "Controller Pak not formatted: FORMAT at the prompt");
        break;
    default:
        p = put_str(p, "Controller Pak: not usable, see Files");
        break;
    }
    *p = 0;
    gfx_box(56, 416, 528, 16, c_win);
    text_at(56, 416, line, fs_state() == FS_PAK ? c_dim : c_amber);
}

static void draw_desktop(int sel)
{
    int i;

    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(48, 48, 544, 400, c_win);
    gfx_box(48, 48, 544, 16, c_bar);
    gfx_frame(48, 48, 544, 400, c_edge);
    text_at(56, 48, "n64forthos", c_ink);
    text_at(320, 48, "d-pad or point and click, A opens", c_ink);
    text_at(56, 64, "A Forth system with a desktop. Pick something:", c_dim);

    for (i = 0; i < NAPPS; i++)
        draw_row(i, i == sel);
    draw_storage();
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

static void draw_app_frame(const char *title)
{
    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(DESK_X, DESK_Y, DESK_W, DESK_H, c_win);
    gfx_box(DESK_X, DESK_Y, DESK_W, 20, c_bar);
    gfx_frame(DESK_X, DESK_Y, DESK_W, DESK_H, c_edge);
    text_at(DESK_X + 8, DESK_Y, title, c_ink);
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

static void open_app(const char *title, const char *source)
{
    u32 mark = forth_mark();
    int top = 0, lines = 0;
    int running = 0, row = 0, rows = 0, passes = 0;
    u32 started = 0;
    const char *p;

    for (p = source; *p; p++)
        if (*p == '\n')
            lines++;

    draw_app_frame(title);
    app_status("compiling...", c_dim);
    forth_set_canvas(CAN_X, CAN_Y, CAN_W, CAN_H);
    forth_eval_lines(source);
    draw_source(source, top);
    app_status("A runs it", c_dim);

    for (;;) {
        u16 pressed;
        const mouse_t *ms;
        int clicked_run = 0;

        input_poll();
        pressed = pressed_now();
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
            draw_source(source, top);
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
                if (!forth_call("ROW")) {
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
                /* NEXT may leave a flag -- true to go round again -- or
                 * nothing.  Taken off the stack either way: left there, a
                 * flag a pass filled the stack in a few minutes. */
                int depth = forth_depth(), again = forth_call("NEXT");

                if (again && forth_depth() > depth)
                    again = forth_pop() != 0;
                if (again) {
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

/* An application that owns the screen: no source pane, no canvas of its own,
 * just a word called once a frame.  The window manager is one of these,
 * because a window manager inside a window is a poor demonstration. */
static void full_app(const char *source)
{
    u32 mark = forth_mark();

    gfx_noclip();
    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, RGB(16, 20, 44));
    forth_set_canvas(0, 16, SCREEN_W, SCREEN_H - 16);
    forth_eval_lines(source);
    forth_call("START");

    for (;;) {
        input_poll();
        if (pressed_now() & PAD_B) {
            gfx_cursor_hide();
            gfx_noclip();
            forth_release(mark);
            forth_set_canvas(0, 0, SCREEN_W, SCREEN_H);
            return;
        }
        if (!forth_call("FRAME")) {
            gfx_noclip();
            con_puts("the application stopped; B to leave\n");
            forth_release(mark);
            forth_set_canvas(0, 0, SCREEN_W, SCREEN_H);
            return;
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
        pressed = pressed_now();
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

        gfx_box(DESK_X + 16, 192, 400, 16, c_win);
        text_at(DESK_X + 16, 192, pak_present()
                ? "pak      a Controller Pak in controller 1"
                : "pak      none in controller 1: files go to RAM", c_text);

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
            gfx_box(DESK_X + 16, 224, 400, 16, c_win);
            text_at(DESK_X + 16, 224, line, c_amber);
        } else if (learn >= 0) {
            gfx_box(DESK_X + 16, 224, 400, 16, c_win);
            text_at(DESK_X + 16, 224, "no keyboard on any channel", c_amber);
        }

        if (ms->present)
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        kernel_status_bar();
        vi_wait_vblank();
    }
}

/* ----------------------------------------------------------- running files */

/* The console, as you left it.  A command, if there is one, is typed for
 * you. */
void console(const char *command)
{
    repl_run_command(command);          /* the prompt repaints itself */
}

static int token_is(const char *t, int n, const char *word)
{
    int i;

    for (i = 0; i < n; i++) {
        char c = t[i];

        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (!word[i] || c != word[i])
            return 0;
    }
    return !word[n];
}

/* Does the source define this word -- as a colon definition, a constant or
 * a variable?  Read token by token, the way Forth would. */
static int defines(const char *src, const char *word)
{
    const char *p = src, *prev = 0;
    int prev_n = 0;

    for (;;) {
        const char *t;
        int n;

        while (*p && (u8)*p <= ' ')
            p++;
        if (!*p)
            return 0;
        t = p;
        while ((u8)*p > ' ')
            p++;
        n = (int)(p - t);
        if (prev && token_is(t, n, word) &&
            (token_is(prev, prev_n, ":") || token_is(prev, prev_n, "CONSTANT") ||
             token_is(prev, prev_n, "VARIABLE")))
            return 1;
        if (token_is(t, n, "\\"))           /* a comment: skip the line */
            while (*p && *p != '\n')
                p++;
        prev = t;
        prev_n = n;
    }
}

/* Open a file as an application if it is one; 0 if it is not, and the
 * caller runs it some other way. */
int desktop_open_file(const char *file)
{
    if (load_app(file))
        return 0;
    if (defines(app_src, "ROWS") && defines(app_src, "ROW")) {
        open_app(file, app_src);
        return 1;
    }
    if (defines(app_src, "FRAME")) {
        full_app(app_src);
        return 1;
    }
    return 0;
}

/* What running a file means depends on what it defines: ROWS and ROW make
 * it an application with a window and a canvas, FRAME makes it one that
 * owns the screen, and anything else is a program for the prompt. */
void desktop_run_file(const char *file, const char *title)
{
    int err = load_app(file);

    if (!err && defines(app_src, "ROWS") && defines(app_src, "ROW")) {
        open_app(title ? title : file, app_src);
    } else if (!err && defines(app_src, "FRAME")) {
        full_app(app_src);
    } else {
        /* INCLUDE says what went wrong, if something did. */
        static char cmd[FS_NAME_MAX + 10];
        char *p = put_str(cmd, "INCLUDE ");

        put_str(p, file);
        p[slen(file)] = 0;
        console(cmd);
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
        pressed = pressed_now();
        ms = input_mouse();
        if (ms->present) {
            if (ms->edges & MOUSE_LEFT) {
                int i;

                for (i = 0; i < NAPPS; i++)
                    if (hit(ms->x, ms->y, 56, LIST_TOP + i * ROW_H, 528, 32)) {
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
            if (apps[sel].kind == APP_FORTH || apps[sel].kind == APP_FULL) {
                desktop_run_file(apps[sel].file, apps[sel].name);
            } else if (apps[sel].kind == APP_DEVICES) {
                devices_app();
            } else if (apps[sel].kind == APP_FILES) {
                files_app();
            } else {
                console(0);             /* returns when L is pressed */
            }
            draw_desktop(sel);
        }

        kernel_status_bar();
        vi_wait_vblank();
    }
}
