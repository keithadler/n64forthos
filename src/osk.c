/* osk.c -- the on-screen keyboard, shared by the prompt and the editor.
 *
 * There is no keyboard port on an N64, so the keyboard is on the screen and
 * the controller drives it: the d-pad (or the stick) moves the highlight
 * and A types the key under it.
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

static const char *const keymap[KB_ROWS] = {
    "1234567890-=",
    "QWERTYUIOP[]",
    "ASDFGHJKL;'\"",
    "ZXCVBNM,./\\?",
    ":!@#$%^&*()+",
    "      \b\b\b\n\n\n",
};

static int sel_row, sel_col;
static int last_row = -1, last_col = -1;

static u16 c_key, c_keyedge, c_label, c_sel, c_sellabel;

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

    x = OSK_X + first * KEY_W;
    y = OSK_Y + row * KEY_H;
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

void osk_draw(void)
{
    int row, col;

    c_key      = RGB(30, 38, 74);
    c_keyedge  = RGB(64, 78, 120);
    c_label    = RGB(205, 213, 228);
    c_sel      = RGB(96, 224, 255);
    c_sellabel = RGB(10, 14, 30);

    gfx_box(OSK_X - 6, OSK_Y - 6, KB_COLS * KEY_W + 12, KB_ROWS * KEY_H + 12,
            RGB(16, 20, 44));
    gfx_frame(OSK_X - 6, OSK_Y - 6, KB_COLS * KEY_W + 12, KB_ROWS * KEY_H + 12,
              c_keyedge);
    for (row = 0; row < KB_ROWS; row++)
        for (col = 0; col < KB_COLS; col++)
            if (col == 0 || key_at(row, col) != key_at(row, col - 1))
                draw_key(row, col);
    last_row = sel_row;
    last_col = sel_col;
}

/* Move the highlight with a direction from osk_direction(). */
void osk_move(u16 dir)
{
    if (dir & PAD_LEFT)  sel_col = (sel_col + KB_COLS - 1) % KB_COLS;
    if (dir & PAD_RIGHT) sel_col = (sel_col + 1) % KB_COLS;
    if (dir & PAD_UP)    sel_row = (sel_row + KB_ROWS - 1) % KB_ROWS;
    if (dir & PAD_DOWN)  sel_row = (sel_row + 1) % KB_ROWS;
    if (sel_row != last_row || sel_col != last_col) {
        if (last_row >= 0)
            draw_key(last_row, last_col);
        draw_key(sel_row, sel_col);
        last_row = sel_row;
        last_col = sel_col;
    }
}

char osk_selected(void)
{
    return key_at(sel_row, sel_col);
}

/* A click: the key under the pointer, which also takes the highlight; 0 if
 * the pointer is not on the keyboard. */
char osk_click(int x, int y)
{
    int col = (x - OSK_X) / KEY_W;
    int row = (y - OSK_Y) / KEY_H;

    if (x < OSK_X || y < OSK_Y || col >= KB_COLS || row >= KB_ROWS)
        return 0;
    sel_row = row;
    sel_col = col;
    osk_move(0);
    return key_at(row, col);
}

/* Direction with auto-repeat, from the d-pad or the stick. */
u16 osk_direction(void)
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
