/* files.c -- the Files window: every file on the Controller Pak (or the RAM
 * disk) and in the ROM, and the things you do with one.
 *
 *   A or Enter        edit it
 *   START or ^R       run it: an app opens in its window, anything else
 *                     runs at the prompt
 *   Z or Delete       delete it (twice, to be sure); a ROM file cannot be
 *   R or ^N           a new file
 *   B or Escape       back to the desktop
 */
#include "n64.h"

#define WIN_X   8
#define WIN_Y   32
#define WIN_W   624
#define WIN_H   434
#define LIST_Y  96
#define NOTE_Y  432
#define LIST_ROWS 20
#define ROW_PX  16

static u16 c_desk, c_win, c_bar, c_edge, c_text, c_dim, c_amber, c_ink,
           c_cyan, c_sel, c_red;

static int count, sel, top;
static const char *note;
static u16 note_colour;
static int armed = -1;                  /* the entry Z was pressed on once */

static int slen(const char *s)
{
    int n = 0;

    while (s[n])
        n++;
    return n;
}

static void text_at(int x, int y, const char *s, u16 c)
{
    gfx_text(x, y, s, slen(s), c);
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_u32(char *p, u32 v, int width)
{
    char tmp[12];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < width--)
        *p++ = ' ';
    while (n--)
        *p++ = tmp[n];
    return p;
}

static void draw_volume(void)
{
    char line[96], *p = line;
    int files = 0, i, size, vol;
    char name[FS_NAME_MAX + 1];

    for (i = 0; i < count; i++)
        if (fs_entry(i, name, &size, &vol) >= 0 && vol != FS_VOL_ROM)
            files++;

    switch (fs_state()) {
    case FS_PAK:
        p = put_str(p, "PAK  Controller Pak: ");
        p = put_u32(p, (u32)files, 0);
        p = put_str(p, files == 1 ? " file, " : " files, ");
        p = put_u32(p, (u32)fs_free_bytes(), 0);
        p = put_str(p, " of ");
        p = put_u32(p, (u32)fs_capacity(), 0);
        p = put_str(p, " bytes free");
        break;
    case FS_RAM:
        p = put_str(p, "RAM  no Controller Pak, so a RAM disk: ");
        p = put_u32(p, (u32)fs_free_bytes(), 0);
        p = put_str(p, " bytes free, gone at power off");
        break;
    case FS_UNFORMATTED:
        p = put_str(p, "PAK  a Controller Pak not formatted for this "
                       "system: FORMAT at the prompt");
        break;
    case FS_CORRUPT:
        p = put_str(p, "PAK  the Controller Pak's directory is damaged: "
                       "FORMAT at the prompt");
        break;
    default:
        p = put_str(p, "PAK  the Controller Pak is not answering");
        break;
    }
    *p = 0;
    gfx_box(WIN_X + 8, 48, WIN_W - 16, 16, c_win);
    text_at(WIN_X + 16, 48, line, fs_state() == FS_PAK ? c_cyan : c_amber);
}

static void draw_entry(int i)
{
    int y = LIST_Y + (i - top) * ROW_PX;
    char name[FS_NAME_MAX + 1], line[64], *p = line;
    int size, vol, on = (i == sel);

    if (i < top || i >= top + LIST_ROWS)
        return;
    gfx_box(WIN_X + 16, y, WIN_W - 32, ROW_PX, on ? c_sel : c_win);
    if (i >= count || fs_entry(i, name, &size, &vol) < 0)
        return;
    p = put_str(p, on ? "> " : "  ");
    p = put_str(p, name);
    while (p < line + 24)
        *p++ = ' ';
    p = put_u32(p, (u32)size, 6);
    p = put_str(p, "  ");
    p = put_str(p, fs_volume_name(vol));
    if (vol == FS_VOL_ROM)
        p = put_str(p, "  read-only");
    *p = 0;
    text_at(WIN_X + 24, y, line, on ? c_amber : vol == FS_VOL_ROM ? c_dim
                                                                  : c_text);
}

static void draw_note(void)
{
    const char *s = note ? note :
        "A edit  START run  Z delete  R new  B back   "
        "(or Enter ^R Del ^N Esc)";

    gfx_box(WIN_X + 8, NOTE_Y, WIN_W - 16, 16, c_win);
    text_at(WIN_X + 16, NOTE_Y, s, note ? note_colour : c_dim);
}

static void say(const char *s, u16 colour)
{
    note = s;
    note_colour = colour;
    draw_note();
}

static void draw_list(void)
{
    int i;

    for (i = top; i < top + LIST_ROWS; i++)
        draw_entry(i);
}

static void draw_all(void)
{
    gfx_cursor_hide();
    count = fs_count();
    if (sel >= count)
        sel = count ? count - 1 : 0;
    gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_desk);
    gfx_box(WIN_X, WIN_Y, WIN_W, WIN_H, c_win);
    gfx_box(WIN_X, WIN_Y, WIN_W, 16, c_bar);
    gfx_frame(WIN_X, WIN_Y, WIN_W, WIN_H, c_edge);
    text_at(WIN_X + 8, WIN_Y, "Files", c_ink);

    draw_volume();
    text_at(WIN_X + 40, 64, "name                     bytes  on", c_dim);
    gfx_hline(WIN_X + 16, 90, WIN_W - 32, c_edge);
    draw_list();
    draw_note();
}

static void pick(int i)
{
    int was = sel, scrolled = 0;

    if (i < 0)
        i = 0;
    if (i >= count)
        i = count - 1;
    sel = i;
    if (sel < top) {
        top = sel;
        scrolled = 1;
    }
    if (sel >= top + LIST_ROWS) {
        top = sel - LIST_ROWS + 1;
        scrolled = 1;
    }
    gfx_cursor_hide();
    if (scrolled) {
        draw_list();
    } else {
        draw_entry(was);
        draw_entry(sel);
    }
}

static int selected(char *name, int *vol)
{
    int size;

    if (count == 0)
        return -1;
    return fs_entry(sel, name, &size, vol);
}

/* A name nobody has used: NEW1.FTH, NEW2.FTH, ... */
static int new_name(char *name)
{
    int n, vol;

    for (n = 1; n < 100; n++) {
        char *p = put_str(name, "NEW");

        p = put_u32(p, (u32)n, 0);
        p = put_str(p, ".FTH");
        *p = 0;
        if (fs_stat(name, slen(name), &vol) == FS_ENOENT)
            return slen(name);
    }
    return 0;
}

void files_app(void)
{
    c_desk  = RGB(16, 20, 44);
    c_win   = RGB(26, 32, 64);
    c_bar   = RGB(96, 224, 255);
    c_edge  = RGB(70, 86, 130);
    c_text  = RGB(205, 213, 228);
    c_dim   = RGB(120, 134, 165);
    c_amber = RGB(255, 190, 90);
    c_ink   = RGB(10, 14, 30);
    c_cyan  = RGB(96, 224, 255);
    c_sel   = RGB(40, 52, 96);
    c_red   = RGB(255, 110, 110);

    input_init();
    fs_mount();                         /* a pak may have come or gone */
    note = 0;
    armed = -1;
    draw_all();

    for (;;) {
        u16 pressed, dir;
        const mouse_t *ms;
        int c, act = 0;                 /* 1 edit, 2 run, 3 delete, 4 new */
        char name[FS_NAME_MAX + 1];
        int vol = 0, n;

        input_poll();
        pressed = input_pressed(0);
        dir = osk_direction();
        ms = input_mouse();
        c = input_getchar();

        if (dir & PAD_UP)
            pick(sel - 1);
        if (dir & PAD_DOWN)
            pick(sel + 1);
        if (c == KEY_UP)
            pick(sel - 1);
        if (c == KEY_DOWN)
            pick(sel + 1);
        if (c == KEY_PGUP || (dir & PAD_LEFT))
            pick(sel - LIST_ROWS);
        if (c == KEY_PGDN || (dir & PAD_RIGHT))
            pick(sel + LIST_ROWS);

        if ((pressed & PAD_A) || c == '\n')
            act = 1;
        if ((pressed & PAD_START) || c == CTRL('R'))
            act = 2;
        if ((pressed & PAD_Z) || c == KEY_DEL)
            act = 3;
        if ((pressed & PAD_R) || c == CTRL('N'))
            act = 4;
        if ((pressed & PAD_B) || c == KEY_ESC) {
            gfx_cursor_hide();
            return;
        }

        if (ms->present) {
            if (ms->edges & MOUSE_LEFT) {
                int i = top + (ms->y - LIST_Y) / ROW_PX;

                if (ms->y >= LIST_Y && i < count && i < top + LIST_ROWS &&
                    ms->x >= WIN_X + 16 && ms->x < WIN_X + WIN_W - 16) {
                    if (i == sel)
                        act = 1;        /* a second click opens it */
                    else
                        pick(i);
                }
            }
        }

        if (act && act != 3 && armed >= 0) {
            armed = -1;
            note = 0;
            draw_note();
        }
        if (act == 1 || act == 2) {
            if ((n = selected(name, &vol)) >= 0) {
                gfx_cursor_hide();
                if (act == 1 && edit_file(name, n) != EDIT_RUN) {
                    draw_all();
                    continue;
                }
                desktop_run_file(name, 0);
                input_init();
                note = 0;
                draw_all();
            }
        } else if (act == 3) {
            if ((n = selected(name, &vol)) < 0) {
                /* nothing to delete */
            } else if (vol == FS_VOL_ROM) {
                say("that file is in ROM: it cannot be deleted", c_red);
            } else if (armed != sel) {
                armed = sel;
                say("press Z (or Delete) again to delete it", c_red);
            } else {
                int err = fs_delete(name, n);

                armed = -1;
                count = fs_count();
                gfx_cursor_hide();
                draw_all();
                say(err ? fs_error(err) : "deleted", err ? c_red : c_amber);
            }
        } else if (act == 4) {
            if ((n = new_name(name)) > 0) {
                gfx_cursor_hide();
                if (edit_file(name, n) == EDIT_RUN) {
                    desktop_run_file(name, 0);
                    input_init();
                }
                note = 0;
                draw_all();
            }
        }

        if (ms->present)                /* the pointer last, over it all */
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        kernel_status_bar();
        vi_wait_vblank();
    }
}
