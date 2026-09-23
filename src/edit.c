/* edit.c -- a text editor, for writing programs on the machine that runs
 * them: over the whole screen, or in a window on the desk.
 *
 * The whole file is held in one buffer, as big as the largest file the pak
 * can hold.  At this size moving the tail along for every keystroke costs
 * less than drawing the line it changed, so there is no gap buffer.
 *
 * With a keyboard: type, arrows move, ^S saves, ^R saves and runs, Esc (or
 * ^Q) leaves, ^K cuts the line (again for the next), ^U puts the cut lines
 * back at the cursor, ^F finds and ^G finds the next.  New lines keep the
 * indent of the one before, except inside a paste, which the page marks
 * with a ^V either end.  With only a controller, over the whole screen: the
 * on-screen keyboard types, the C buttons move the cursor, START saves and
 * L leaves; R shows or hides the on-screen keyboard.  A click puts the
 * cursor where you clicked.
 *
 * There is one buffer, so one file open at a time: the desk's window and
 * the full-screen editor take turns with it.
 */
#include "n64.h"

#define TEXT_TOP   2                    /* rows 0 and 1: status and title */
#define GUTTER     48                   /* line numbers, then the text */
#define MSG_ROW    28                   /* 29 is lost to the overscan */

static char buf[FS_FILE_MAX + 1];
static int len, cur, goal = -1;
static int top_line, hscroll;
static int modified, confirm_quit;
static int osk_on;
static int pasting;                     /* between two ^Vs: no auto-indent */
static char fname[FS_NAME_MAX + 1];
static int fname_len;
static int vol;                         /* where the file came from */
static const char *message;
static char clip[2048];                 /* lines cut with ^K, for ^U */
static int clip_len, cutting;
static char query[40];                  /* what ^F looks for */
static int query_len, finding;
static char find_line[64];
static char status_line[96];
static u16 message_colour;

/* The view: where the text is drawn.  The whole screen, or a window. */
static int windowed;
static int vx, vy = TEXT_TOP * 16, vw = SCREEN_W;
static int win_rows, win_msg_y;
static int session;                     /* a file open in a window */

static u16 c_bg, c_text, c_dim, c_bar, c_ink, c_amber, c_cursor, c_red;

static void colours(void)
{
    c_bg     = RGB(10, 14, 30);
    c_text   = RGB(205, 213, 228);
    c_dim    = RGB(110, 125, 155);
    c_bar    = RGB(96, 224, 255);
    c_ink    = RGB(10, 14, 30);
    c_amber  = RGB(255, 190, 90);
    c_cursor = RGB(255, 190, 90);
    c_red    = RGB(255, 110, 110);
}

static int text_rows(void)
{
    if (windowed)
        return win_rows;
    return osk_on ? 18 : MSG_ROW - TEXT_TOP;
}

static int text_cols(void)
{
    int n = (vw - GUTTER - 8) / 8;

    return n < 1 ? 1 : n;
}

/* ------------------------------------------------------------ geometry */

static int line_start(int pos)
{
    while (pos > 0 && buf[pos - 1] != '\n')
        pos--;
    return pos;
}

static int line_end(int pos)
{
    while (pos < len && buf[pos] != '\n')
        pos++;
    return pos;
}

static int line_of(int pos)
{
    int i, n = 0;

    for (i = 0; i < pos; i++)
        if (buf[i] == '\n')
            n++;
    return n;
}

static int lines_total(void)
{
    return line_of(len) + 1;
}

/* Where line n begins, or len if there are not that many. */
static int start_of_line(int n)
{
    int i;

    for (i = 0; i < len && n > 0; i++)
        if (buf[i] == '\n')
            n--;
    return n > 0 ? len : i;
}

/* ------------------------------------------------------------- drawing */

static void put_num(char *out, int v, int width)
{
    int i;

    for (i = width - 1; i >= 0; i--) {
        out[i] = (char)(v ? '0' + v % 10 : ' ');
        v /= 10;
    }
    if (out[width - 1] == ' ')
        out[width - 1] = '0';
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

/* A number, left-aligned. */
static char *put_int(char *p, int v)
{
    char tmp[12];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n--)
        *p++ = tmp[n];
    return p;
}

static void draw_row(int row)
{
    int y = vy + row * 16;
    int n = top_line + row;
    int p = start_of_line(n);
    int x, col, cols = text_cols();
    char num[5];
    u16 colour = c_text;

    gfx_box(vx, y, vw, 16, c_bg);
    if (n >= lines_total())
        return;
    put_num(num, n + 1, 4);
    gfx_text(vx + 8, y, num, 4, c_dim);

    /* A comment line keeps the colour the app windows give it. */
    {
        int q = p;

        while (q < len && buf[q] == ' ')
            q++;
        if (q < len && buf[q] == '\\' && (q + 1 >= len || buf[q + 1] <= ' '))
            colour = c_dim;
    }
    for (col = 0; p < len && buf[p] != '\n'; col++, p++) {
        if (col < hscroll)
            continue;
        if (col - hscroll >= cols)
            break;
        x = vx + GUTTER + (col - hscroll) * 8;
        if (p == cur)
            gfx_glyph(x, y, buf[p], c_ink, c_cursor, 1);
        else
            gfx_glyph(x, y, buf[p], colour, c_bg, 1);
    }
    if (p == cur) {                     /* the cursor past the last character */
        col = cur - line_start(cur) - hscroll;
        if (col >= 0 && col < cols)
            gfx_box(vx + GUTTER + col * 8, y, 8, 16, c_cursor);
    }
}

static void draw_text(void)
{
    int r;

    for (r = 0; r < text_rows(); r++)
        draw_row(r);
}

static void draw_message(void);

/* Which file, where, and where the cursor is: a title bar over the whole
 * screen; in a window, whose own title names the file, the message line. */
static void draw_title(void)
{
    char t[81], *p = t;
    int line = line_of(cur) + 1, col = cur - line_start(cur) + 1;

    if (windowed) {
        if (!message)
            draw_message();
        return;
    }
    p = put_str(p, " EDIT  ");
    p = put_str(p, fname);
    p = put_str(p, vol == FS_VOL_ROM ? "  (ROM: saving keeps a copy on " :
                   "  on ");
    p = put_str(p, fs_volume_name(vol == FS_VOL_ROM ? FS_VOL_PAK : vol));
    if (vol == FS_VOL_ROM) {
        p = put_str(p, ")");
        if (fs_state() == FS_RAM) {
            p -= 4;
            p = put_str(p, "RAM)");
        }
    } else if (fs_state() == FS_RAM) {
        p -= 3;
        p = put_str(p, "RAM");
    }
    p = put_str(p, modified ? "  *" : "   ");
    while (p < t + 58)
        *p++ = ' ';
    p = put_str(p, "line ");
    p = put_int(p, line);
    p = put_str(p, " col ");
    p = put_int(p, col);
    while (p < t + 80)
        *p++ = ' ';
    *p = 0;
    gfx_box(0, 16, SCREEN_W, 16, c_bar);
    gfx_text(0, 16, t, 80, c_ink);
}

static void draw_message(void)
{
    int y = windowed ? win_msg_y : osk_on ? 20 * 16 : MSG_ROW * 16;
    const char *s = message;
    u16 colour = message_colour;
    int n = 0;

    if (!s && windowed) {
        char *p = status_line;

        p = put_str(p, "line ");
        p = put_int(p, line_of(cur) + 1);
        p = put_str(p, " col ");
        p = put_int(p, cur - line_start(cur) + 1);
        p = put_str(p, modified ? "  *  " : "     ");
        p = put_str(p, "^S save ^R run Esc close ^O other");
        *p = 0;
        s = status_line;
        colour = c_dim;
    } else if (!s) {
        s = osk_on ? "A type  B rub out  C buttons move  START save  L leave"
                   : "^S save  ^R run  Esc leave  ^K cut  ^U paste  ^F find"
                     "  R keys";
        colour = c_dim;
    }
    while (s[n])
        n++;
    gfx_box(vx, y, vw, 16, c_bg);
    gfx_text(vx + 8, y, s, n, colour);
}

static void say(const char *s, u16 colour)
{
    message = s;
    message_colour = colour;
    draw_message();
}

static void layout(void)
{
    gfx_cursor_hide();
    if (windowed) {
        gfx_box(vx, vy, vw, win_msg_y + 16 - vy, c_bg);
    } else {
        gfx_box(0, 16, SCREEN_W, SCREEN_H - 16, c_bg);
        if (osk_on)
            osk_draw();
    }
    draw_title();
    draw_text();
    draw_message();
}

/* Keep the cursor on the screen; say whether the view moved. */
static int follow(void)
{
    int line = line_of(cur), col = cur - line_start(cur);
    int moved = 0, cols = text_cols();

    if (line < top_line) {
        top_line = line;
        moved = 1;
    }
    if (line >= top_line + text_rows()) {
        top_line = line - text_rows() + 1;
        moved = 1;
    }
    if (col < hscroll) {
        hscroll = col < 8 ? 0 : col - 8;
        moved = 1;
    }
    if (col >= hscroll + cols) {
        hscroll = col - cols + 16;
        moved = 1;
    }
    return moved;
}

/* After a change: repaint what it touched and no more. */
static void refresh(int old_line, int structural, int any)
{
    int moved = follow(), now = line_of(cur) - top_line;

    gfx_cursor_hide();
    if (moved) {
        draw_text();
        draw_title();
    } else if (structural) {
        int r;

        for (r = old_line < now ? old_line : now; r < text_rows(); r++)
            if (r >= 0)
                draw_row(r);
        draw_title();
    } else if (now != old_line || any) {
        if (old_line >= 0 && old_line < text_rows())
            draw_row(old_line);
        if (now != old_line)
            draw_row(now);
        draw_title();
    }
}

/* ------------------------------------------------------------- editing */

static int insert(const char *s, int n)
{
    int i;

    if (len + n > FS_FILE_MAX) {
        say("the file is as big as a file can be", c_red);
        return 0;
    }
    for (i = len - 1; i >= cur; i--)
        buf[i + n] = buf[i];
    for (i = 0; i < n; i++)
        buf[cur + i] = s[i];
    len += n;
    cur += n;
    buf[len] = 0;
    modified = 1;
    return 1;
}

static void cut(int at, int n)
{
    int i;

    if (at < 0 || n <= 0 || at + n > len)
        return;
    for (i = at; i + n < len; i++)
        buf[i] = buf[i + n];
    len -= n;
    buf[len] = 0;
    if (cur > at)
        cur = cur - n < at ? at : cur - n;
    modified = 1;
}

static int rub_out(void)                /* returns 1 if it joined lines */
{
    int joined;

    if (cur <= 0)
        return 0;
    joined = buf[cur - 1] == '\n';
    cut(cur - 1, 1);
    return joined;
}

static void move_vertical(int lines)
{
    int line = line_of(cur), target = line + lines, total = lines_total();
    int col, p, e;

    if (goal < 0)
        goal = cur - line_start(cur);
    col = goal;
    if (target < 0)
        target = 0;
    if (target >= total)
        target = total - 1;
    p = start_of_line(target);
    e = line_end(p);
    cur = (p + col > e) ? e : p + col;
}

static int save(void)
{
    int err = fs_write(fname, fname_len, buf, len);

    if (err) {
        say(fs_error(err), c_red);
        return 0;
    }
    modified = 0;
    confirm_quit = 0;
    if (vol == FS_VOL_ROM)
        vol = (fs_state() == FS_RAM) ? FS_VOL_RAM : FS_VOL_PAK;
    say(fs_state() == FS_RAM ? "saved to the RAM disk (no Controller Pak: "
                               "gone at power off)"
                             : "saved to the Controller Pak", c_amber);
    return 1;
}

/* Case-insensitively, from just after the cursor, round to the start. */
static int find_next(void)
{
    int n, i, k;

    if (!query_len)
        return 0;
    for (n = 1; n <= len; n++) {
        i = (cur + n) % (len + 1);
        for (k = 0; k < query_len && i + k < len; k++) {
            char a = buf[i + k], b = query[k];

            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b)
                break;
        }
        if (k == query_len) {
            cur = i;
            return 1;
        }
    }
    return 0;
}

static void show_query(void)
{
    char *p = find_line;
    int i;

    p = put_str(p, "find: ");
    for (i = 0; i < query_len; i++)
        *p++ = query[i];
    p = put_str(p, "_    Enter finds, Esc stops");
    *p = 0;
    say(find_line, c_amber);
}

/* ---------------------------------------------------------------- keys */

#define KEY_GOES_ON (-1)

/* What one key does.  EDIT_QUIT or EDIT_RUN to leave, KEY_GOES_ON to carry
 * on; *structural says lines were joined or split, *keep_goal that the
 * cursor moved up or down and should remember its column. */
static int edit_key(int c, int *structural, int *keep_goal)
{
    if (!c)
        return KEY_GOES_ON;
    if (finding) {                      /* typing the thing to find */
        if (c == '\n' || c == CTRL('G')) {
            finding = 0;
            if (find_next())
                say("found", c_amber);
            else
                say("not found", c_red);
        } else if (c == KEY_ESC || c == CTRL('F')) {
            finding = 0;
            message = 0;
            draw_message();
        } else if (c == '\b') {
            if (query_len)
                query_len--;
            show_query();
        } else if (c >= ' ' && c <= '~' && query_len < (int)sizeof(query)) {
            query[query_len++] = (char)c;
            show_query();
        }
        goal = -1;
        return KEY_GOES_ON;
    }
    if (c != CTRL('K'))                 /* any other key ends a run of cuts */
        cutting = 0;
    if (c != CTRL('S') && c != KEY_ESC && c != CTRL('Q'))
        confirm_quit = 0;
    if (message && c != CTRL('S') && c != CTRL('F') && c != CTRL('G') &&
        !confirm_quit) {
        message = 0;
        draw_message();
    }

    if (c == CTRL('F')) {
        finding = 1;
        query_len = 0;
        show_query();
    } else if (c == CTRL('G')) {
        if (find_next())
            say("found", c_amber);
        else
            say("not found", c_red);
    } else if (c == CTRL('U')) {
        if (clip_len && insert(clip, clip_len))
            *structural = 1;
    } else if (c == CTRL('S')) {
        save();
    } else if (c == CTRL('R')) {
        if (save())
            return EDIT_RUN;
    } else if (c == KEY_ESC || c == CTRL('Q')) {
        if (modified && !confirm_quit) {
            confirm_quit = 1;
            say("not saved: leave again to throw the changes away, "
                "or save first", c_red);
        } else {
            return EDIT_QUIT;
        }
    } else if (c == CTRL('K')) {
        int s = line_start(cur), e = line_end(cur), n, k;

        n = e - s + (e < len ? 1 : 0);
        if (!cutting)
            clip_len = 0;               /* a fresh run of cuts */
        for (k = 0; k < n && clip_len < (int)sizeof(clip); k++)
            clip[clip_len++] = buf[s + k];
        if (e >= len && clip_len < (int)sizeof(clip))
            clip[clip_len++] = '\n';    /* the last line had none */
        cutting = 1;
        cut(s, n);
        cur = s;
        *structural = 1;
    } else if (c == CTRL('V')) {
        pasting = !pasting;             /* the page brackets a paste in these */
    } else if (c == '\n') {
        char indent[40];
        int s = line_start(cur), k = 0;

        indent[k++] = '\n';
        while (!pasting && s + k - 1 < cur && buf[s + k - 1] == ' ' &&
               k < 39) {
            indent[k] = ' ';
            k++;
        }
        insert(indent, k);
        *structural = 1;
    } else if (c == '\b') {
        *structural |= rub_out();
    } else if (c == KEY_DEL) {
        if (cur < len) {
            *structural |= buf[cur] == '\n';
            cut(cur, 1);
        }
    } else if (c == '\t') {
        insert("   ", 3);
    } else if (c >= ' ' && c <= '~') {
        char ch = (char)c;

        insert(&ch, 1);
    } else if (c == KEY_LEFT) {
        if (cur > 0)
            cur--;
    } else if (c == KEY_RIGHT) {
        if (cur < len)
            cur++;
    } else if (c == KEY_UP || c == KEY_DOWN) {
        move_vertical(c == KEY_UP ? -1 : 1);
        *keep_goal = 1;
    } else if (c == KEY_PGUP || c == KEY_PGDN) {
        move_vertical(c == KEY_PGUP ? -text_rows() : text_rows());
        *keep_goal = 1;
    } else if (c == KEY_HOME) {
        cur = line_start(cur);
    } else if (c == KEY_END) {
        cur = line_end(cur);
    }
    if (!*keep_goal)
        goal = -1;
    return KEY_GOES_ON;
}

/* Read the file into the buffer; 0, or an error for the caller to report. */
static int load(const char *name, int name_len)
{
    int i, n;

    for (i = 0; i < name_len && i < FS_NAME_MAX; i++)
        fname[i] = name[i];
    fname[i] = 0;
    fname_len = i;
    pasting = finding = cutting = 0;
    message = 0;
    n = fs_read(fname, fname_len, buf, FS_FILE_MAX);
    if (n == FS_ENOENT) {
        len = 0;
        vol = (fs_state() == FS_RAM) ? FS_VOL_RAM : FS_VOL_PAK;
        message = "a new file: it is written when you save";
        message_colour = c_amber;
    } else if (n < 0) {
        len = 0;
        return n;
    } else {
        len = n;
        fs_stat(fname, fname_len, &vol);
    }
    buf[len] = 0;
    cur = 0;
    goal = -1;
    top_line = hscroll = 0;
    modified = confirm_quit = 0;
    return 0;
}

static int same_name(const char *name, int name_len)
{
    int i;

    if (name_len != fname_len)
        return 0;
    for (i = 0; i < name_len; i++) {
        char a = name[i], b = fname[i];

        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b)
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------ the whole screen */

int edit_file(const char *name, int name_len)
{
    int err, result = EDIT_QUIT;

    colours();
    if (session && modified) {
        con_printf("%s is open on the desk, not saved: save it there first\n",
                   fname);
        return EDIT_QUIT;
    }
    session = 0;
    windowed = 0;
    vx = 0;
    vy = TEXT_TOP * 16;
    vw = SCREEN_W;
    input_init();
    osk_on = !input_keyboard()->present;
    if ((err = load(name, name_len)) != 0) {
        con_puts(fname);
        con_printf(": %s\n", fs_error(err));
        return EDIT_QUIT;
    }
    layout();

    for (;;) {
        int c, old_line = line_of(cur) - top_line, whole = 0;
        int structural = 0, keep_goal = 0, act;
        u16 dir, pressed;
        const mouse_t *ms;

        input_poll();
        dir = osk_direction();
        pressed = input_pressed(0);
        ms = input_mouse();
        c = input_getchar();

        /* START saves and L leaves, as ^S and Esc would. */
        if (!c && (pressed & PAD_START))
            c = CTRL('S');
        if (!c && (pressed & PAD_L))
            c = CTRL('Q');
        act = edit_key(c, &structural, &keep_goal);
        if (act != KEY_GOES_ON) {
            result = act;
            break;
        }

        /* The controller. */
        if (osk_on) {
            osk_move(dir);
            if (pressed & PAD_A) {
                char k = osk_selected();

                if (k == '\n') {
                    insert("\n", 1);
                    structural = 1;
                } else if (k == '\b') {
                    structural |= rub_out();
                } else {
                    insert(&k, 1);
                }
            }
            if (pressed & PAD_B)
                structural |= rub_out();
            if (pressed & PAD_Z)
                insert(" ", 1);
            {
                const pad_t *pad = input_pad(0);
                static int hold;
                u16 cb = pad->buttons & (PAD_CUP | PAD_CDOWN | PAD_CLEFT |
                                         PAD_CRIGHT);
                u16 go = (cb & pressed) | ((hold > 18 && !(hold & 3)) ? cb : 0);

                hold = cb ? hold + 1 : 0;
                if ((go & PAD_CLEFT) && cur > 0)
                    cur--;
                if ((go & PAD_CRIGHT) && cur < len)
                    cur++;
                if (go & (PAD_CUP | PAD_CDOWN)) {
                    move_vertical((go & PAD_CUP) ? -1 : 1);
                    keep_goal = 1;
                }
            }
        } else {
            if ((dir & PAD_LEFT) && cur > 0)
                cur--;
            if ((dir & PAD_RIGHT) && cur < len)
                cur++;
            if (dir & (PAD_UP | PAD_DOWN)) {
                move_vertical((dir & PAD_UP) ? -1 : 1);
                keep_goal = 1;
            }
            if (pressed & PAD_B)
                structural |= rub_out();
        }
        if (pressed & PAD_R) {
            osk_on = !osk_on;
            whole = 1;
        }

        /* The mouse: click to put the cursor there, or on a key. */
        if (ms->present && (ms->edges & MOUSE_LEFT)) {
            int row = (ms->y - vy) / 16;

            gfx_cursor_hide();
            if (osk_on && ms->y >= OSK_Y) {
                char k = osk_click(ms->x, ms->y);

                if (k == '\n' || (k >= ' ' && k <= '~')) {
                    insert(&k, 1);
                    structural = k == '\n';
                } else if (k == '\b') {
                    structural |= rub_out();
                }
            } else if (ms->y >= vy && row < text_rows()) {
                int p = start_of_line(top_line + row);
                int col = (ms->x - GUTTER) / 8 + hscroll, e = line_end(p);

                if (top_line + row < lines_total()) {
                    if (col < 0)
                        col = 0;
                    cur = (p + col > e) ? e : p + col;
                }
            }
        }
        if (!keep_goal && (pressed || dir || (ms->edges & MOUSE_LEFT)))
            goal = -1;

        if (whole) {
            follow();
            layout();
        } else {
            refresh(old_line, structural,
                    c || pressed || dir || ms->edges);
        }

        if (ms->present)
            gfx_cursor_show(ms->x, ms->y, RGB(255, 255, 255), RGB(0, 0, 0));
        kernel_status_bar();
        vi_wait_vblank();
    }
    gfx_cursor_hide();
    return result;
}

/* ------------------------------------------------------ in a window
 *
 * The desk opens a file with edit_window_open() and then calls
 * edit_window_step() once a frame from the window's draw word, with the
 * window's rectangle.  Keys only while the window is in front.
 */
static int win_x = -1, win_y, win_w, win_h, win_seen;

int edit_window_open(const char *name, int name_len)
{
    int err;

    colours();
    if (session && same_name(name, name_len))
        return 0;                       /* already open: carry on with it */
    if (session && modified)
        return FS_EBUSY;
    if ((err = load(name, name_len)) != 0)
        return err;
    session = 1;
    win_x = -1;                         /* lay it out afresh */
    return 0;
}

const char *edit_window_name(int *n)
{
    *n = session ? fname_len : 0;
    return fname;
}

int edit_window_step(int x, int y, int w, int h, int repaint, int focused)
{
    int status = 0, c;

    if (!session)
        return EDIT_STEP_CLOSED;
    colours();
    windowed = 1;
    osk_on = 0;
    if (x != win_x || y != win_y || w != win_w || h != win_h ||
        repaint != win_seen) {
        win_x = x;
        win_y = y;
        win_w = w;
        win_h = h;
        win_seen = repaint;
        /* On the character grid, like everything else. */
        vx = (x + 7) & ~7;
        vy = (y + 1 + 15) & ~15;
        vw = x + w - vx;
        win_rows = (y + h - vy) / 16 - 1;
        if (win_rows < 1)
            win_rows = 1;
        win_msg_y = vy + win_rows * 16;
        follow();
        layout();
    }
    while (focused && (c = input_getchar())) {
        int old_line = line_of(cur) - top_line, structural = 0, keep_goal = 0;
        int act;

        if (c == CTRL('O')) {           /* the other window, please */
            status |= EDIT_STEP_SWITCH;
            break;
        }
        act = edit_key(c, &structural, &keep_goal);
        if (act == EDIT_QUIT) {
            session = 0;
            windowed = 0;
            return status | EDIT_STEP_CLOSED;
        }
        if (act == EDIT_RUN)
            status |= EDIT_STEP_RUN;
        refresh(old_line, structural, 1);
    }
    windowed = 0;                       /* the full-screen editor's view */
    return status;
}
