/* repl.c -- the prompt.
 *
 * There is no keyboard port on an N64, so the keyboard is on the screen and
 * the controller drives it: the d-pad (or the stick) moves the highlight, A
 * types the key under it, B rubs out, Z is a space and START runs the line.
 *
 * The grid is uniform -- six rows of twelve -- which keeps the movement
 * arithmetic trivial and lets a test drive it by counting presses.  Keys
 * that repeat across neighbouring cells are drawn as one wide key, which is
 * how SPACE, BSP and ENTER get their width.
 */
#include "n64.h"

#define KB_ROWS 6
#define KB_COLS 12
#define KEY_W   40
#define KEY_H   20
#define KB_X    80
#define KB_Y    348
#define HINT_ROW 20
#define LINE_MAX 44        /* what fits in the console's own columns */

static const char *const keymap[KB_ROWS] = {
    "1234567890-=",
    "QWERTYUIOP[]",
    "ASDFGHJKL;'\"",
    "ZXCVBNM,./\\?",
    ":!@#$%^&*()+",
    "      \b\b\b\n\n\n",
};

static int sel_row, sel_col = 0;
static int last_row = -1, last_col = -1;
static char line[LINE_MAX + 1];
static int len;
static int blink;
static int shown_len = -1;
static int shown_blink = -1;
static int shown_row = -1;

static u16 c_key, c_keyedge, c_label, c_sel, c_sellabel, c_dim, c_amber, c_text;

static char key_at(int row, int col)
{
    return keymap[row][col];
}

static const char *key_label(char c)
{
    switch (c) {
    case ' ':  return "SPACE";
    case '\b': return "BSP";
    case '\n': return "ENTER";
    default:   return 0;
    }
}

/* Draw the run of identical cells that contains (row, col) as one key. */
static void draw_key(int row, int col)
{
    char c = key_at(row, col);
    int first = col, last = col;
    int x, y, w, selected;
    const char *label;

    while (first > 0 && key_at(row, first - 1) == c)
        first--;
    while (last < KB_COLS - 1 && key_at(row, last + 1) == c)
        last++;

    x = KB_X + first * KEY_W;
    y = KB_Y + row * KEY_H;
    w = (last - first + 1) * KEY_W;
    selected = (sel_row == row && sel_col >= first && sel_col <= last);

    gfx_box(x + 1, y + 1, w - 2, KEY_H - 2, selected ? c_sel : c_key);
    gfx_frame(x + 1, y + 1, w - 2, KEY_H - 2, c_keyedge);

    label = key_label(c);
    if (label) {
        int n = 0;
        while (label[n])
            n++;
        gfx_text(x + (w - n * 8) / 2, y + 2, label, n,
                 selected ? c_sellabel : c_label);
    } else {
        gfx_glyph(x + (w - 8) / 2, y + 2, c,
                  selected ? c_sellabel : c_label, 0, 0);
    }
}

static void draw_keyboard(void)
{
    int row, col;

    gfx_box(KB_X - 6, KB_Y - 6, KB_COLS * KEY_W + 12, KB_ROWS * KEY_H + 12,
            RGB(16, 20, 44));
    gfx_frame(KB_X - 6, KB_Y - 6, KB_COLS * KEY_W + 12, KB_ROWS * KEY_H + 12,
              c_keyedge);
    for (row = 0; row < KB_ROWS; row++)
        for (col = 0; col < KB_COLS; col++)
            if (col == 0 || key_at(row, col) != key_at(row, col - 1))
                draw_key(row, col);
}

static void draw_hint(void)
{
    static const char hint[] =
        "d-pad or mouse   A type   B rub out   Z space   START run   L desktop";

    con_erase_row(HINT_ROW);
    /* On the character grid, so a test can read it back. */
    gfx_text(KB_X - 8, HINT_ROW * 16, hint, (int)sizeof(hint) - 1, c_dim);
}

/* The line being typed lives on whatever row the console cursor is on. */
static void draw_line(void)
{
    int row = con_row();
    int x;

    con_erase_row(row);
    x = gfx_text(16, row * 16, "ok> ", 4, c_dim);
    x = gfx_text(x, row * 16, line, len, c_amber);
    if (blink < 16)
        gfx_box(x, row * 16 + 1, 8, 14, c_amber);
}

static void type_char(char c)
{
    if (c == '\b') {
        if (len)
            line[--len] = 0;
        return;
    }
    if (c == '\n') {
        return;                 /* handled by the caller, which submits */
    }
    if (len < LINE_MAX) {
        line[len++] = c;
        line[len] = 0;
    }
}

static void submit(void)
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

    if (len)
        forth_eval(line);
    if (con_col() != 0)
        con_putc('\n');

    len = 0;
    line[0] = 0;
    shown_len = -1;
}

/* Direction with auto-repeat, from the d-pad or the stick. */
static u16 direction(void)
{
    static int hold;
    static u16 last;
    const pad_t *p = input_pad(0);
    u16 now = p->buttons & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);

    if (p->stick_x > 48)  now |= PAD_RIGHT;
    if (p->stick_x < -48) now |= PAD_LEFT;
    if (p->stick_y > 48)  now |= PAD_UP;
    if (p->stick_y < -48) now |= PAD_DOWN;

    if (!now) {
        hold = 0;
        last = 0;
        return 0;
    }
    if (now != last) {
        hold = 0;
        last = now;
        return now;
    }
    hold++;
    if (hold > 18 && (hold & 3) == 0)
        return now;
    return 0;
}

void repl_run(void)
{
    c_key      = RGB(30, 38, 74);
    c_keyedge  = RGB(64, 78, 120);
    c_label    = RGB(205, 213, 228);
    c_sel      = RGB(96, 224, 255);
    c_sellabel = RGB(10, 14, 30);
    c_dim      = RGB(110, 125, 155);
    c_amber    = RGB(255, 190, 90);
    c_text     = RGB(205, 213, 228);

    input_init();
    last_row = last_col = -1;
    shown_len = -1;
    draw_hint();
    draw_keyboard();

    for (;;) {
        u16 dir, pressed;

        input_poll();
        dir = direction();
        pressed = input_pressed(0);

        {   /* A real keyboard, if the adapter is presenting one. */
            int c = input_getchar();

            if (c == '\n') {
                submit();
            } else if (c) {
                type_char((char)c);
            }
        }
        {   /* A real mouse: click a key to type it. */
            const mouse_t *ms = input_mouse();

            if (ms->present) {
                if (ms->edges & MOUSE_LEFT) {
                    int col = (ms->x - KB_X) / KEY_W;
                    int row = (ms->y - KB_Y) / KEY_H;

                    if (col >= 0 && col < KB_COLS && row >= 0 && row < KB_ROWS) {
                        char c = key_at(row, col);

                        gfx_cursor_hide();
                        sel_row = row;
                        sel_col = col;
                        if (c == '\n')
                            submit();
                        else
                            type_char(c);
                    }
                }
                gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
            }
        }

        if (dir & PAD_LEFT)  sel_col = (sel_col + KB_COLS - 1) % KB_COLS;
        if (dir & PAD_RIGHT) sel_col = (sel_col + 1) % KB_COLS;
        if (dir & PAD_UP)    sel_row = (sel_row + KB_ROWS - 1) % KB_ROWS;
        if (dir & PAD_DOWN)  sel_row = (sel_row + 1) % KB_ROWS;

        if (pressed & PAD_A) {
            char c = key_at(sel_row, sel_col);

            if (c == '\n')
                submit();
            else
                type_char(c);
        }
        if (pressed & PAD_B)
            type_char('\b');
        if (pressed & PAD_Z)
            type_char(' ');
        if (pressed & PAD_START)
            submit();
        if (pressed & PAD_R) {
            con_clear();
            shown_len = -1;
        }
        if (pressed & PAD_L) {
            gfx_cursor_hide();
            return;
        }                     /* back to the desktop */

        if (sel_row != last_row || sel_col != last_col) {
            if (last_row >= 0)
                draw_key(last_row, last_col);
            draw_key(sel_row, sel_col);
            last_row = sel_row;
            last_col = sel_col;
        }

        blink = (blink + 1) & 31;
        if (len != shown_len || (blink < 16) != (shown_blink < 16) ||
            con_row() != shown_row) {
            draw_line();
            shown_len = len;
            shown_blink = blink;
            shown_row = con_row();
        }

        kernel_status_bar();
        vi_wait_vblank();
    }
}
