/* repl.c -- the prompt.
 *
 * Two layouts.  With only a controller, the keyboard is on the screen (see
 * osk.c): the d-pad moves the highlight, A types the key under it, B rubs
 * out, Z is a space and START runs the line, and the console keeps to the
 * left of the screen above it.  With a real keyboard -- a Randnet, or a
 * BlueRetro adapter presenting one -- the on-screen keyboard goes away and
 * the console takes the whole screen, with the arrow keys recalling earlier
 * lines.  R switches between the two whenever you like.
 */
#include "n64.h"

#define HINT_ROW   20
#define LINE_MAX   120
#define HISTORY    16

static char line[LINE_MAX + 1];
static int len;
static int blink;
static int shown_len = -1;
static int shown_blink = -1;
static int shown_row = -1;
static int osk_on;              /* the on-screen keyboard is showing */
static int active;              /* the prompt is on the screen */

static char history[HISTORY][LINE_MAX + 1];
static int hist_count, hist_at;

static u16 c_dim, c_amber, c_text;

static int line_max(void)
{
    int n = con_cols() - 5;     /* "ok> " and the cursor */

    return n > LINE_MAX ? LINE_MAX : n;
}

/* Lay the screen out for the current mode, and put the console's text back:
 * also what anything that has painted over the prompt -- the editor, an app
 * window -- calls to get it back. */
static void layout(void)
{
    gfx_cursor_hide();
    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, RGB(10, 14, 30));
    if (osk_on) {
        static const char hint[] =
            "d-pad or mouse   A type   B rub out   Z space   START run   "
            "L desktop";

        con_place(16, 0, 48, 2, 19);
        con_redraw();
        /* On the character grid, so a test can read it back. */
        gfx_text(OSK_X - 8, HINT_ROW * 16, hint, (int)sizeof(hint) - 1, c_dim);
        osk_draw();
    } else {
        static const char hint[] =
            "keyboard   Enter run   up/down history   R on-screen keys   "
            "Tab desktop";

        /* Row 29 is below what a television shows, so the hint is on 28. */
        con_place(16, 0, CON_MAX_COLS, 2, 27);
        con_redraw();
        gfx_text(16, 28 * 16, hint, (int)sizeof(hint) - 1, c_dim);
    }
    if (len > line_max())
        line[len = line_max()] = 0;
    shown_len = -1;
}

void repl_repaint(void)
{
    if (active)
        layout();
}

/* The line being typed lives on whatever row the console cursor is on. */
static void draw_line(void)
{
    int row = con_row();
    int x, y = con_origin_y() + row * 16;
    int room = con_cols() - 5, from = len > room ? len - room : 0;

    con_erase_row(row);
    x = gfx_text(con_origin_x(), y, "ok> ", 4, c_dim);
    x = gfx_text(x, y, line + from, len - from, c_amber);
    if (blink < 16)
        gfx_box(x, y + 1, 8, 14, c_amber);
}

static void type_char(char c)
{
    if (c == '\b') {
        if (len)
            line[--len] = 0;
        return;
    }
    if ((u8)c < ' ' || (u8)c > '~')
        return;                 /* control and editing keys: not text */
    if (len < line_max()) {
        line[len++] = c;
        line[len] = 0;
    }
}

static int same_line(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void remember(void)
{
    int i;

    if (!len)
        return;
    if (hist_count && same_line(history[(hist_count - 1) % HISTORY], line))
        return;                 /* the same line again: once is enough */
    for (i = 0; i <= len; i++)
        history[hist_count % HISTORY][i] = line[i];
    hist_count++;
}

static void recall(int step)
{
    int oldest = hist_count > HISTORY ? hist_count - HISTORY : 0;
    int i;

    hist_at += step;
    if (hist_at < oldest)
        hist_at = oldest;
    if (hist_at >= hist_count) {
        hist_at = hist_count;
        len = 0;
        line[0] = 0;
        return;
    }
    for (i = 0; history[hist_at % HISTORY][i] && i < line_max(); i++)
        line[i] = history[hist_at % HISTORY][i];
    line[i] = 0;
    len = i;
}

static void submit_line(int nested)
{
    int row = con_row();

    con_erase_row(row);
    con_at(row, 0);
    con_color(c_dim);
    con_puts("ok> ");
    con_color(c_amber);
    con_puts(line);
    con_putc('\n');
    con_color(c_text);

    remember();
    hist_at = hist_count;
    {   /* the line is cleared first: what it runs may use the prompt */
        char run[LINE_MAX + 1];
        int i;

        for (i = 0; i <= len; i++)
            run[i] = line[i];
        len = 0;
        line[0] = 0;
        if (run[0]) {
            if (nested)
                forth_eval_nested(run);
            else
                forth_eval(run);
        }
    }
    if (con_col() != 0)
        con_putc('\n');
    shown_len = -1;
}

static void submit(void)
{
    submit_line(0);
}

void repl_run(void)
{
    repl_run_command(0);
}

/* The prompt, with a command already typed and run -- which is how Files
 * runs a program that is not an application. */
void repl_run_command(const char *command)
{
    c_dim      = RGB(110, 125, 155);
    c_amber    = RGB(255, 190, 90);
    c_text     = RGB(205, 213, 228);

    input_init();
    osk_on = !input_keyboard()->present;
    hist_at = hist_count;
    active = 1;
    layout();
    if (command) {
        for (len = 0; command[len] && len < line_max(); len++)
            line[len] = command[len];
        line[len] = 0;
        submit();
    }

    for (;;) {
        u16 dir, pressed;
        int c;

        input_poll();
        dir = osk_direction();
        pressed = input_pressed(0);

        /* A real keyboard, if the adapter is presenting one. */
        c = input_getchar();
        if (c == '\n')
            submit();
        else if (c == KEY_UP)
            recall(-1);
        else if (c == KEY_DOWN)
            recall(1);
        else if (c == '\t')
            pressed |= PAD_L;
        else if (c)
            type_char((char)c);

        {   /* A real mouse: click a key to type it. */
            const mouse_t *ms = input_mouse();

            if (ms->present) {
                if ((ms->edges & MOUSE_LEFT) && osk_on) {
                    char k;

                    gfx_cursor_hide();
                    k = osk_click(ms->x, ms->y);
                    if (k == '\n')
                        submit();
                    else if (k)
                        type_char(k);
                }
            }
        }

        if (osk_on) {
            osk_move(dir);
            if (pressed & PAD_A) {
                char k = osk_selected();

                if (k == '\n')
                    submit();
                else
                    type_char(k);
            }
            if (pressed & PAD_B)
                type_char('\b');
            if (pressed & PAD_Z)
                type_char(' ');
        } else {
            if (dir & PAD_UP)
                recall(-1);
            if (dir & PAD_DOWN)
                recall(1);
        }
        if (pressed & PAD_START)
            submit();
        if (pressed & PAD_R) {
            osk_on = !osk_on;
            layout();
        }
        if ((pressed & PAD_L) || forth_quit_requested()) {
            gfx_cursor_hide();
            active = 0;
            con_place(16, 0, 48, 2, 19);
            return;
        }                     /* back to the desktop */

        blink = (blink + 1) & 31;
        if (len != shown_len || (blink < 16) != (shown_blink < 16) ||
            con_row() != shown_row) {
            draw_line();
            shown_len = len;
            shown_blink = blink;
            shown_row = con_row();
        }

        /* The pointer last, over whatever this frame drew. */
        if (input_mouse()->present)
            gfx_cursor_show(input_mouse()->x, input_mouse()->y,
                            RGB(255, 255, 255), RGB(0, 0, 0));
        kernel_status_bar();
        vi_wait_vblank();
    }
}

/* ------------------------------------------------------ a prompt in a window
 *
 * Returns 1 when something it ran had the whole screen, 2 when ^O asks for
 * the other window: bits, as both can happen in one frame.
 *
 * The same prompt, one frame at a time, inside whatever rectangle a window
 * manager lends it -- called from the window's draw word, so it runs beside
 * everything else on the desk.  It takes keys only while its window is in
 * front.  A line is evaluated where it stands, on stacks of its own above
 * the running window manager's (forth_eval_nested), so an error in it
 * unwinds only itself.  Returns non-zero when something it ran -- EDIT, an
 * app -- has had the whole screen, and the desk wants painting again.
 */
static int win_x = -1, win_y, win_w, win_h, win_seen;
static int win_last_focus = -2;           /* -2: never run; else 0 or not */
static u32 keys_frame = ~0u;            /* when a window last took keys */

/* A prompt or an editor in a window has the keys this frame: Escape and ^C
 * are typing, not the break key, and not the desk's to leave on. */
void keys_claimed(void)
{
    keys_frame = vi_frames();
    forth_keyboard_break(0);
}

int console_has_keys(void)
{
    return vi_frames() - keys_frame < 2;
}

int repl_window_step(int x, int y, int w, int h, int repaint, int focused)
{
    /* The text on the screen's character grid, wherever the window is:
     * crisp, and the same cells as everything else draws in. */
    int ox = (x + 3 + 7) & ~7, oy = (y + 1 + 15) & ~15;
    int cols = (x + w - 2 - ox) / 8, rows = (y + h - 1 - oy) / 16;
    int c, taken = 0;

    c_dim   = RGB(110, 125, 155);
    c_amber = RGB(255, 190, 90);
    c_text  = RGB(205, 213, 228);
    if (cols > CON_MAX_COLS)
        cols = CON_MAX_COLS;
    if (rows > 28)
        rows = 28;
    if (cols < 12 || rows < 2)
        return 0;
    if (x != win_x || y != win_y || w != win_w || h != win_h ||
        repaint != win_seen) {
        win_x = x;
        win_y = y;
        win_w = w;
        win_h = h;
        win_seen = repaint;
        con_place(ox, oy, cols, 0, rows - 1);
        con_redraw();
        shown_len = -1;
    }
    if (focused)
        keys_claimed();                 /* its keys are typing, not break */
    if (focused != win_last_focus)
        shown_len = -1;
    win_last_focus = focused;

    while (focused && (c = input_getchar())) {
        if (c == CTRL('O')) {           /* the other window, please */
            taken |= 2;
            break;
        }
        if (c == '\n') {
            submit_line(1);
            if (forth_screen_taken())
                taken |= 1;
            con_place(ox, oy, cols, 0, rows - 1);
        } else if (c == KEY_UP) {
            recall(-1);
        } else if (c == KEY_DOWN) {
            recall(1);
        } else {
            type_char((char)c);
        }
    }
    blink = focused ? (blink + 1) & 31 : 16;
    if (len != shown_len || (blink < 16) != (shown_blink < 16) ||
        con_row() != shown_row) {
        draw_line();
        shown_len = len;
        shown_blink = blink;
        shown_row = con_row();
    }
    return taken;
}
