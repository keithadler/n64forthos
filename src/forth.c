/* forth.c -- the Forth kernel: dictionary, inner interpreter, compiler.
 *
 * Token threaded.  A word is a header (link, flags, name) followed by an
 * execution token: one cell holding a primitive number, where DOCOL means
 * "the cells after me are a thread of execution tokens".  Addresses handed
 * to Forth are real machine addresses, so @ and ! reach the hardware and
 * SEE decompiles what is actually in memory.
 */
#include "n64.h"
#include "prims.h"

/* The dictionary lives in RDRAM at a fixed address rather than in .bss, so
 * the kernel image stays small and the dictionary can grow without moving. */
#define DICT_BASE   0x80300000u
#define DICT_BYTES  (512 * 1024)
#define DSTACK_MAX  256
#define RSTACK_MAX  256
#define CSTACK_MAX  64          /* control-flow stack, compile time only */
#define NAME_MAX    31
#define IMMEDIATE   0x01

/* Memory Forth is allowed to reach: RDRAM through either alias, and the
 * hardware register block.  Everything else is refused rather than faulted. */
#define RAM_LO      0x80000000u
#define RAM_HI      0x80800000u
#define URAM_LO     0xA0000000u
#define URAM_HI     0xA0800000u
#define REPORT_ADDR  0xA0380000u    /* where REPORT leaves its result block */
#define REPORT_MAGIC 0x54535431u    /* "TST1" */

#define IO_LO       0xA4000000u
#define IO_HI       0xA4900000u

static u8 *const dict = (u8 *)DICT_BASE;
static u8 *const dict_end = (u8 *)(DICT_BASE + DICT_BYTES);
static u8 *dp;                  /* next free dictionary byte */
static cell latest;             /* address of the newest header, 0 = none */
static cell primitives_end;     /* nothing at or below this may be forgotten */
/* A few cells of slack either side: compiled code checks the stack every
 * time it moves the pointer, but an operation reads its operands before the
 * check fires, and this is what it reads instead of somebody else's data. */
#define DGUARD 8
static cell dstack_area[DGUARD + DSTACK_MAX + DGUARD];
static cell *const dstack = dstack_area + DGUARD;
static int dsp;
static cell rstack[RSTACK_MAX];
static int rsp;
static cell cstack[CSTACK_MAX];
/* Marks on the control stack that are not addresses: what ?DO and CASE
 * leave for LOOP and ENDCASE to find. */
#define CS_QDO   ((cell)0xC0DE0001)
/* LEAVE's forward jumps, resolved by the LOOP that ends their loop.  A zero
 * separates one loop's from the next one out. */
#define LEAVES_MAX 32
static cell leaves[LEAVES_MAX];
static int nleaves;
#define CS_CASE  ((cell)0xC0DE0002)
static int csp;
static cell def_header;         /* definition under construction, 0 = none */
static cell def_prev_latest;
static int state;               /* 0 = interpreting, 1 = compiling */
static int base = 10;
static int aborted;
static int interrupted;         /* the break key: stop the whole file too */
static u32 ticks;

static void error(const char *msg, const char *name, int len);

/* Called where a running program goes round: loops, and the helpers that
 * compiled code calls.  Every so often it looks for the break key. */
static void tick(void)
{
    if (++ticks & 0x7FFF)
        return;
    if (input_break_check()) {
        interrupted = 1;
        error("interrupted", 0, 0);
    }
}

/* The input stream the outer interpreter is chewing through. */
static const char *in_p;
static const char *in_end;


/* ---------------------------------------------------------------- stacks */

static void error(const char *msg, const char *name, int len)
{
    u16 save = con_get_color();

    con_color(RGB(255, 96, 96));
    con_puts("\n? ");
    con_puts(msg);
    if (name && len) {
        con_puts(": ");
        while (len--)
            con_putc(*name++);
    }
    con_putc('\n');
    con_color(save);

    /* A failed definition must not be left half-linked in the dictionary. */
    if (state && def_header) {
        latest = def_prev_latest;
        dp = (u8 *)(u32)def_header;
    }
    def_header = 0;
    state = 0;
    dsp = rsp = csp = 0;
    nleaves = 0;
    aborted = 1;
}

static void push(cell v)
{
    if (dsp >= DSTACK_MAX) {
        error("stack overflow", 0, 0);
        return;
    }
    dstack[dsp++] = v;
}

static cell pop(void)
{
    if (dsp <= 0) {
        error("stack underflow", 0, 0);
        return 0;
    }
    return dstack[--dsp];
}

static void rpush(cell v)
{
    if (rsp >= RSTACK_MAX) {
        error("return stack overflow", 0, 0);
        return;
    }
    rstack[rsp++] = v;
}

static cell rpop(void)
{
    if (rsp <= 0) {
        error("return stack underflow", 0, 0);
        return 0;
    }
    return rstack[--rsp];
}

/* ----------------------------------------------------------- dictionary */

static int slen(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (int)(p - s);
}

static u8 *align_up(u8 *p)
{
    return (u8 *)(((u32)p + 3) & ~3u);
}

/* Is there room for n more bytes of dictionary? */
static int room(int n)
{
    if (dp + n + 8 > dict_end) {
        error("dictionary full", 0, 0);
        return 0;
    }
    return 1;
}

/* May Forth touch size bytes at a?  align is 0, 2 or 4. */
static int addr_ok(cell a, u32 size, u32 align)
{
    u32 v = (u32)a;

    if (align && (v & (align - 1))) {
        error("misaligned address", 0, 0);
        return 0;
    }
    if ((v >= RAM_LO && v + size <= RAM_HI) ||
        (v >= URAM_LO && v + size <= URAM_HI) ||
        (v >= IO_LO && v + size <= IO_HI))
        return 1;
    error("address out of range", 0, 0);
    return 0;
}

/* A file name handed over from Forth: its bytes must be readable, and it
 * must be a name the file system could hold. */
static int name_ok(cell a, cell u)
{
    if (u < 0 || !addr_ok(a, (u32)u, 0))
        return 0;
    if (!fs_name_ok((const char *)(u32)a, (int)u)) {
        error(fs_error(FS_EBADNAME), (const char *)(u32)a, u > 24 ? 24 : (int)u);
        return 0;
    }
    return 1;
}

static void cpush(cell v)
{
    if (csp >= CSTACK_MAX) {
        error("control stack overflow", 0, 0);
        return;
    }
    cstack[csp++] = v;
}

static cell cpop(void)
{
    if (csp <= 0) {
        error("unstructured: no matching IF/BEGIN/DO", 0, 0);
        return 0;
    }
    return cstack[--csp];
}

static void comma(cell v)
{
    if (!room(4))
        return;
    dp = align_up(dp);
    *(cell *)dp = v;
    dp += 4;
}

static cell header(const char *name, int len, int flags)
{
    cell h;
    int i;

    if (len < 1 || len > NAME_MAX) {
        error("bad name length", name, len);
        return 0;
    }
    if (!room(len + 16))
        return 0;
    dp = align_up(dp);
    h = (cell)(u32)dp;
    *(cell *)dp = latest;
    dp += 4;
    *dp++ = (u8)flags;
    *dp++ = (u8)len;
    for (i = 0; i < len; i++)
        *dp++ = (u8)name[i];
    dp = align_up(dp);
    latest = h;
    return h;                   /* execution token is at dp, written next */
}

static cell name_to_xt(cell h)
{
    u8 len = *(u8 *)(u32)(h + 5);
    return (cell)(u32)align_up((u8 *)(u32)(h + 6 + len));
}

static int same(const char *a, const char *b, int n)
{
    while (n--) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
    }
    return 1;
}

static cell find(const char *name, int len, int *flags)
{
    cell h = latest;

    while (h) {
        if (*(u8 *)(u32)(h + 5) == len &&
            same((const char *)(u32)(h + 6), name, len)) {
            if (flags)
                *flags = *(u8 *)(u32)(h + 4);
            return name_to_xt(h);
        }
        h = *(cell *)(u32)h;
    }
    return 0;
}

static void defprim(const char *name, int code, int flags)
{
    header(name, (int)slen(name), flags);
    comma(code);
}

/* -------------------------------------------------------------- numbers */

static int to_number(const char *s, int len, cell *out)
{
    int neg = 0, b = base, any = 0;
    cell v = 0;

    if (len && (*s == '$' || *s == '#')) {
        b = (*s == '$') ? 16 : 10;
        s++;
        len--;
    }
    if (len > 1 && *s == '-') {
        neg = 1;
        s++;
        len--;
    }
    while (len--) {
        int c = (unsigned char)*s++, d;

        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else
            return 0;
        if (d >= b)
            return 0;
        v = v * b + d;
        any = 1;
    }
    if (!any)
        return 0;
    *out = neg ? -v : v;
    return 1;
}

static void print_number(cell v, int b)
{
    char buf[12];
    int n = 0;
    u32 u;

    if (b == 10 && v < 0) {
        con_putc('-');
        u = (u32)(-v);
    } else {
        u = (u32)v;
    }
    do {
        u32 d = u % (u32)b;
        buf[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        u /= (u32)b;
    } while (u);
    while (n--)
        con_putc(buf[n]);
    con_putc(' ');
}

/* --------------------------------------------------------- input stream */

static const char *word_start;
static int word_len;

static int next_word(void)
{
    while (in_p < in_end && (u8)*in_p <= ' ')
        in_p++;
    if (in_p >= in_end)
        return 0;
    word_start = in_p;
    while (in_p < in_end && (u8)*in_p > ' ')
        in_p++;
    word_len = (int)(in_p - word_start);
    return 1;
}

/* ------------------------------------------------- the inner interpreter */

extern u16 *fb_uncached(void);

static void compile_string(int prim);

/* ------------------------------------------------------ 16.16 fixed point
 *
 * The VR4300 multiplies 32x32 into 64 for free, but dividing a 64-bit value
 * needs a helper the freestanding build has no library for, so here is one:
 * restoring division, thirty-two rounds, quotient assumed to fit.
 */
static u32 udiv64_32(unsigned long long n, u32 d)
{
    unsigned long long rem = n >> 32;
    u32 q = 0;
    int i;

    if (!d || rem >= d)
        return 0x7FFFFFFFu;                 /* saturate rather than lie */
    for (i = 31; i >= 0; i--) {
        rem = (rem << 1) | ((n >> i) & 1);
        if (rem >= d) {
            rem -= d;
            q |= 1u << i;
        }
    }
    return q;
}

static u32 usqrt64(unsigned long long v)
{
    unsigned long long rem = 0, root = 0;
    int i;

    for (i = 0; i < 32; i++) {
        root <<= 1;
        rem = (rem << 2) | ((v >> 62) & 3);
        v <<= 2;
        if (root < rem) {
            root++;
            rem -= root;
            root++;
        }
    }
    return (u32)(root >> 1);
}

static cell fixed_mul(cell a, cell b)
{
    return (cell)(((long long)a * (long long)b) >> 16);
}

/* a / b in 16.16.
 *
 * The obvious way -- shift the numerator up 16 places and do a 64-bit
 * division -- costs a 32-round loop, and the ray tracer spends most of its
 * time here: a dozen divisions a ray.  So do it as the VR4300 would: the
 * integer part with one hardware divide, then the fraction out of the
 * remainder with one or two more.  Exact, and several times faster.
 */
static cell fixed_div(cell a, cell b)
{
    int neg = 0;
    u32 ua, ub, whole, rem, frac;

    if (a < 0) { a = -a; neg ^= 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    ua = (u32)a;
    ub = (u32)b;
    if (!ub)
        return 0;

    whole = ua / ub;
    rem = ua % ub;
    if (whole > 0x7FFFu)
        return neg ? (cell)0x80000000u : (cell)0x7FFFFFFFu;   /* saturate */

    if (ub <= 0x7FFFu) {
        frac = (rem << 16) / ub;                  /* rem < ub, so this fits */
    } else if (ub <= 0x7FFFFFu) {
        u32 t = (rem << 8) / ub;                  /* two eight-bit steps */
        u32 r1 = (rem << 8) % ub;
        frac = (t << 8) | ((r1 << 8) / ub);
    } else {
        frac = udiv64_32(((unsigned long long)rem) << 16, ub);
    }
    {
        u32 q = (whole << 16) | (frac & 0xFFFFu);
        return neg ? -(cell)q : (cell)q;
    }
}

/* Where the desktop lets the running app draw. */
static cell canvas[4] = { 0, 0, SCREEN_W, SCREEN_H };

void forth_set_canvas(int x, int y, int w, int h)
{
    canvas[0] = x;
    canvas[1] = y;
    canvas[2] = w;
    canvas[3] = h;
}

static void prim(int code, cell xt, cell **ipp)
{
    cell a, b, c;

    switch (code) {
    case P_EXIT:    *ipp = (cell *)(u32)rpop(); break;
    case P_LIT:     push(*(*ipp)++); break;
    case P_SLIT: {
        cell len = *(*ipp)++;
        const char *s = (const char *)(u32)*ipp;
        int i;
        for (i = 0; i < len; i++)
            con_putc(s[i]);
        *ipp += (len + 3) / 4;
        break;
    }
    case P_BRANCH:
    case P_AGAINBR:
        if ((cell *)(u32)**ipp < *ipp)
            tick();
        *ipp = (cell *)(u32)**ipp;
        break;
    case P_ZBRANCH:
        if (pop() == 0) {
            if ((cell *)(u32)**ipp < *ipp)
                tick();                 /* UNTIL going round again */
            *ipp = (cell *)(u32)**ipp;
        } else {
            (*ipp)++;
        }
        break;
    case P_DO:      b = pop(); a = pop(); rpush(a); rpush(b); break;
    case P_LOOP: {
        cell limit, idx;
        idx = rpop();
        limit = rpop();
        idx++;
        tick();
        if (idx < limit && !aborted) {
            rpush(limit);
            rpush(idx);
            *ipp = (cell *)(u32)**ipp;
        } else {
            (*ipp)++;
        }
        break;
    }
    case P_QDO:                         /* ( limit start -- ) or skip */
        b = pop(); a = pop();
        if (a == b) {
            *ipp = (cell *)(u32)**ipp;
        } else {
            rpush(a);
            rpush(b);
            (*ipp)++;
        }
        break;
    case P_PLOOP: {                     /* ( n -- ) done when it crosses */
        cell limit, idx, n = pop(), next;

        idx = rpop();
        limit = rpop();
        next = (cell)((u32)idx + (u32)n);
        tick();
        if (((idx - limit) ^ (next - limit)) >= 0 && !aborted) {
            rpush(limit);
            rpush(next);
            *ipp = (cell *)(u32)**ipp;
        } else {
            (*ipp)++;
        }
        break;
    }
    case P_DOCREATE: {                  /* push the body; run DOES> if any */
        cell does = *(cell *)(u32)(xt + 4);

        push(xt + 8);
        if (does) {
            rpush((cell)(u32)*ipp);
            *ipp = (cell *)(u32)does;
        }
        break;
    }
    case P_PDOES: {                     /* the word just CREATEd does this */
        cell last = latest ? name_to_xt(latest) : 0;

        if (!last || *(cell *)(u32)last != P_DOCREATE) {
            error("DOES> without CREATE", 0, 0);
            break;
        }
        *(cell *)(u32)(last + 4) = (cell)(u32)*ipp;
        *ipp = (cell *)(u32)rpop();     /* and the defining word is done */
        break;
    }
    case P_CREATE:
        if (!next_word()) {
            error("name expected", 0, 0);
            break;
        }
        if (header(word_start, word_len, 0)) {
            comma(P_DOCREATE);
            comma(0);
        }
        break;
    case P_TOBODY:
        a = pop();
        if (addr_ok(a, 8, 4) && *(cell *)(u32)a == P_DOCREATE)
            push(a + 8);
        else
            error("not a CREATEd word", 0, 0);
        break;
    case P_CHAR:
        if (next_word())
            push((u8)word_start[0]);
        else
            error("CHAR of what?", 0, 0);
        break;
    case P_ACCEPT: {                    /* ( addr max -- n ) a typed line */
        cell max = pop(), addr = pop();
        int n = 0, c;

        if (max < 0 || !addr_ok(addr, (u32)max, 0))
            break;
        for (;;) {
            gfx_box(16 + (con_col() + 2) * 8 - 16, con_row() * 16 + 1, 8, 14,
                    RGB(255, 190, 90));
            while (!(c = input_getchar())) {
                vi_wait_vblank();
                input_poll();
                kernel_status_bar();
            }
            gfx_box(16 + (con_col() + 2) * 8 - 16, con_row() * 16 + 1, 8, 14,
                    RGB(10, 14, 30));
            if (c == '\n' || c == KEY_ESC)
                break;
            if (c == '\b') {
                if (n > 0 && con_col() > 0) {
                    n--;
                    con_at(con_row(), con_col() - 1);
                    con_putc(' ');
                    con_at(con_row(), con_col() - 1);
                }
                continue;
            }
            if (c >= ' ' && c <= '~' && n < max) {
                ((char *)(u32)addr)[n++] = (char)c;
                con_putc((char)c);
            }
        }
        con_putc('\n');
        push(n);
        break;
    }
    case P_I:       push(rstack[rsp - 1]); break;
    case P_UNLOOP:  (void)rpop(); (void)rpop(); break;
    case P_J:       push(rstack[rsp - 3]); break;
    case P_LEAVE:                       /* out of the loop, now */
        (void)rpop();
        (void)rpop();
        *ipp = (cell *)(u32)**ipp;
        break;

    case P_DUP:     a = pop(); push(a); push(a); break;
    case P_2DUP:    b = pop(); a = pop(); push(a); push(b); push(a); push(b);
                    break;
    case P_2DROP:   (void)pop(); (void)pop(); break;
    case P_2SWAP: {
        cell d = pop(), c2 = pop();
        b = pop(); a = pop();
        push(c2); push(d); push(a); push(b);
        break;
    }
    case P_QDUP:    a = pop(); push(a); if (a) push(a); break;
    case P_DROP:    (void)pop(); break;
    case P_SWAP:    b = pop(); a = pop(); push(b); push(a); break;
    case P_OVER:    b = pop(); a = pop(); push(a); push(b); push(a); break;
    case P_ROT:     c = pop(); b = pop(); a = pop(); push(b); push(c); push(a); break;
    case P_NIP:     b = pop(); (void)pop(); push(b); break;
    case P_TUCK:    b = pop(); a = pop(); push(b); push(a); push(b); break;
    case P_PICK:    a = pop();
                    if (a < 0 || a >= dsp) error("PICK out of range", 0, 0);
                    else push(dstack[dsp - 1 - a]); break;
    case P_ROLL: {
        int i;
        a = pop();
        if (a < 0 || a >= dsp) {
            error("ROLL out of range", 0, 0);
            break;
        }
        b = dstack[dsp - 1 - a];
        for (i = dsp - 1 - (int)a; i < dsp - 1; i++)
            dstack[i] = dstack[i + 1];
        dstack[dsp - 1] = b;
        break;
    }
    case P_DEPTH:   push(dsp); break;
    case P_TOR:     rpush(pop()); break;
    case P_RFROM:   push(rpop()); break;
    case P_RFETCH:  push(rstack[rsp - 1]); break;

    case P_ADD:     b = pop(); a = pop(); push(a + b); break;
    case P_SUB:     b = pop(); a = pop(); push(a - b); break;
    case P_MUL:     b = pop(); a = pop(); push(a * b); break;
    case P_DIV:     b = pop(); a = pop();
                    if (!b) error("divide by zero", 0, 0); else push(a / b); break;
    case P_MOD:     b = pop(); a = pop();
                    if (!b) error("divide by zero", 0, 0); else push(a % b); break;
    case P_NEGATE:  push(-pop()); break;
    case P_ABS:     a = pop(); push(a < 0 ? -a : a); break;
    case P_MIN:     b = pop(); a = pop(); push(a < b ? a : b); break;
    case P_MAX:     b = pop(); a = pop(); push(a > b ? a : b); break;
    case P_1PLUS:   push(pop() + 1); break;
    case P_1MINUS:  push(pop() - 1); break;
    case P_2MUL:    push(pop() << 1); break;
    case P_2DIV:    push(pop() >> 1); break;

    case P_AND:     b = pop(); a = pop(); push(a & b); break;
    case P_OR:      b = pop(); a = pop(); push(a | b); break;
    case P_XOR:     b = pop(); a = pop(); push(a ^ b); break;
    case P_INVERT:  push(~pop()); break;
    case P_LSHIFT:  b = pop(); a = pop(); push((cell)((u32)a << b)); break;
    case P_RSHIFT:  b = pop(); a = pop(); push((cell)((u32)a >> b)); break;

    case P_EQ:      b = pop(); a = pop(); push(a == b ? -1 : 0); break;
    case P_NE:      b = pop(); a = pop(); push(a != b ? -1 : 0); break;
    case P_LT:      b = pop(); a = pop(); push(a < b ? -1 : 0); break;
    case P_GT:      b = pop(); a = pop(); push(a > b ? -1 : 0); break;
    case P_ULT:     b = pop(); a = pop(); push((u32)a < (u32)b ? -1 : 0); break;
    case P_ZEQ:     push(pop() == 0 ? -1 : 0); break;
    case P_ZLT:     push(pop() < 0 ? -1 : 0); break;
    case P_ZGT:     push(pop() > 0 ? -1 : 0); break;

    case P_FETCH:   a = pop(); if (addr_ok(a, 4, 4)) push(*(cell *)(u32)a); break;
    case P_STORE:   a = pop(); b = pop();
                    if (addr_ok(a, 4, 4)) *(cell *)(u32)a = b; break;
    case P_CFETCH:  a = pop(); if (addr_ok(a, 1, 0)) push(*(u8 *)(u32)a); break;
    case P_CSTORE:  a = pop(); b = pop();
                    if (addr_ok(a, 1, 0)) *(u8 *)(u32)a = (u8)b; break;
    case P_PLUSSTORE: a = pop(); b = pop();
                    if (addr_ok(a, 4, 4)) *(cell *)(u32)a += b; break;
    case P_FILL: {
        cell val = pop(), n = pop(), addr = pop();
        if (n < 0 || !addr_ok(addr, (u32)n, 0))
            break;
        while (n-- > 0)
            *(u8 *)(u32)addr++ = (u8)val;
        break;
    }

    case P_DOT:     print_number(pop(), base); break;
    case P_UDOT:    con_printf("%u ", (u32)pop()); break;
    case P_HDOT:    con_printf("%x ", (u32)pop()); break;
    case P_DOTS: {
        int i;
        con_printf("<%d> ", dsp);
        for (i = 0; i < dsp; i++)
            print_number(dstack[i], base);
        break;
    }
    case P_EMIT:    con_putc((char)pop()); break;
    case P_CR:      con_putc('\n'); break;
    case P_SPACE:   con_putc(' '); break;
    case P_SPACES:  a = pop(); while (a-- > 0) con_putc(' '); break;
    case P_TYPE: {
        cell n = pop(), addr = pop();
        if (n < 0 || !addr_ok(addr, (u32)n, 0))
            break;
        while (n-- > 0)
            con_putc(*(char *)(u32)addr++);
        break;
    }

    case P_HERE:    push((cell)(u32)dp); break;
    case P_ALLOT:   a = pop();
                    if (a < 0) {
                        if ((u32)(dp + a) < (u32)dict)
                            error("ALLOT below the dictionary", 0, 0);
                        else
                            dp += a;
                    } else if (room((int)a)) {
                        dp += a;
                    }
                    break;
    case P_COMMA:   comma(pop()); break;
    case P_CCOMMA:  a = pop(); if (room(1)) *dp++ = (u8)a; break;
    case P_CELLS:   push(pop() * 4); break;
    case P_CELLPLUS: push(pop() + 4); break;

    case P_DOVAR:   push(xt + 4); break;
    case P_DOCON:   push(*(cell *)(u32)(xt + 4)); break;

    case P_DECIMAL: base = 10; break;
    case P_HEX:     base = 16; break;
    case P_BASE:    push((cell)(u32)&base); break;
    case P_ABORT:   dsp = 0; error("aborted", 0, 0); break;
    case P_EXECUTE: {
        extern void forth_execute(cell);
        a = pop();
        if ((u32)a < (u32)dict || (u32)a >= (u32)dp || ((u32)a & 3))
            error("not an execution token", 0, 0);
        else
            forth_execute(a);
        break;
    }

    /* --- console and video ------------------------------------------- */
    case P_PAGE:    con_clear(); break;
    case P_AT:      b = pop(); a = pop(); con_at(a, b); break;
    case P_INK:     con_color((u16)pop()); break;
    case P_RGBW:    c = pop(); b = pop(); a = pop(); push(RGB(a, b, c)); break;
    case P_FB:      push((cell)(u32)gfx_fb()); break;
    case P_CLS:     gfx_cls((u16)pop()); break;
    case P_PLOT: {
        cell col = pop(), y = pop(), x = pop();
        gfx_plot((int)x, (int)y, (u16)col);
        break;
    }
    case P_BOX: {
        cell col = pop(), h = pop(), w = pop(), y = pop(), x = pop();
        gfx_box((int)x, (int)y, (int)w, (int)h, (u16)col);
        break;
    }
    case P_FRAME: {
        cell col = pop(), h = pop(), w = pop(), y = pop(), x = pop();
        gfx_frame((int)x, (int)y, (int)w, (int)h, (u16)col);
        break;
    }
    case P_HLINE: {
        cell col = pop(), w = pop(), y = pop(), x = pop();
        gfx_hline((int)x, (int)y, (int)w, (u16)col);
        break;
    }
    case P_VLINE: {
        cell col = pop(), h = pop(), y = pop(), x = pop();
        gfx_vline((int)x, (int)y, (int)h, (u16)col);
        break;
    }
    case P_LINE: {
        cell col = pop(), y1 = pop(), x1 = pop(), y0 = pop(), x0 = pop();
        gfx_line((int)x0, (int)y0, (int)x1, (int)y1, (u16)col);
        break;
    }
    case P_DRAWTEXT: {
        /* ( addr len x y colour -- ) */
        cell col = pop(), y = pop(), x = pop(), len = pop(), addr = pop();
        if (len < 0 || len > 256 || !addr_ok(addr, (u32)len, 0))
            break;
        gfx_text((int)x, (int)y, (const char *)(u32)addr, (int)len, (u16)col);
        break;
    }
    case P_BLIT:
    case P_BLITKEY: {
        /* ( addr x y w h -- ) */
        cell h = pop(), w = pop(), y = pop(), x = pop(), addr = pop();
        if (w < 0 || h < 0 || w * h > 0x40000 ||
            !addr_ok(addr, (u32)(w * h * 2), 2))
            break;
        gfx_blit((const u16 *)(u32)addr, (int)x, (int)y, (int)w, (int)h,
                 code == P_BLITKEY);
        break;
    }
    case P_FMUL:    b = pop(); a = pop(); push(fixed_mul(a, b)); break;
    case P_FDIV:    b = pop(); a = pop();
                    if (!b) error("divide by zero", 0, 0);
                    else push(fixed_div(a, b)); break;
    case P_FSQRT:   a = pop();
                    if (a < 0) a = 0;
                    push((cell)usqrt64(((unsigned long long)(u32)a) << 16));
                    break;
    case P_KEYSET:  b = pop(); a = pop(); input_key_map((int)a, (int)b); break;
    case P_MOUSEX:  push(input_mouse()->x); break;
    case P_MOUSEY:  push(input_mouse()->y); break;
    case P_MOUSEB:  push(input_mouse()->buttons); break;
    case P_MOUSEHIT: push(input_mouse()->edges); break;
    case P_POLL:    input_poll(); break;
    case P_BUTTONS: push(input_buttons(0)); break;
    case P_PRESSED: push(input_pressed(0)); break;
    case P_TICKS:   push((cell)vi_frames()); break;
    case P_CURSOR:  gfx_cursor_show(input_mouse()->x, input_mouse()->y,
                                    RGB(255, 255, 255), RGB(0, 0, 0)); break;
    case P_HIDECUR: gfx_cursor_hide(); break;
    case P_NOCLIP:  gfx_noclip(); break;
    case P_CLIP: {
        cell h = pop(), w = pop(), y = pop(), x = pop();
        gfx_clip((int)x, (int)y, (int)w, (int)h);
        break;
    }
    case P_SETCANVAS: {
        cell h = pop(), w = pop(), y = pop(), x = pop();
        forth_set_canvas((int)x, (int)y, (int)w, (int)h);
        break;
    }
    case P_CANVASX: push(canvas[0]); break;
    case P_CANVASY: push(canvas[1]); break;
    case P_CANVASW: push(canvas[2]); break;
    case P_CANVASH: push(canvas[3]); break;
    case P_SQRUN: {
        /* runtime of S": ( -- addr len ) */
        cell len = *(*ipp)++;
        push((cell)(u32)*ipp);
        push(len);
        *ipp += (len + 3) / 4;
        break;
    }
    case P_FRAMES:  push((cell)vi_frames()); break;
    case P_REPORT: {
        /* ( passes fails -- )  Leave a result block somewhere a debugger --
         * or tools/check.py, reading RDRAM out of the emulator -- can find
         * it without a screen. */
        volatile u32 *r = (volatile u32 *)REPORT_ADDR;
        cell fails = pop(), passes = pop();

        r[0] = REPORT_MAGIC;
        r[1] = (u32)passes;
        r[2] = (u32)fails;
        break;
    }
    case P_VSYNC:   vi_wait_vblank(); break;

    /* ------------------------------------------------------ files
     *
     * Names are (c-addr u) pairs, as S" and PARSE-NAME leave them.  An ior
     * is 0 for success and a negative code otherwise; .IOR says what it
     * means. */
    case P_PARSENAME:
        if (next_word()) {
            push((cell)(u32)word_start);
            push(word_len);
        } else {
            push((cell)(u32)in_p);
            push(0);
        }
        break;
    case P_LOADFILE: {                  /* ( addr max c-addr u -- n ior ) */
        cell u = pop(), name = pop(), max = pop(), addr = pop();
        int n;

        if (aborted || !name_ok(name, u) || max < 0 ||
            !addr_ok(addr, (u32)max, 0))
            break;
        n = fs_read((const char *)(u32)name, (int)u, (char *)(u32)addr,
                    (int)max);
        push(n < 0 ? 0 : n);
        push(n < 0 ? n : 0);
        break;
    }
    case P_SAVEFILE: {                  /* ( addr n c-addr u -- ior ) */
        cell u = pop(), name = pop(), n = pop(), addr = pop();

        if (aborted || !name_ok(name, u) || n < 0 ||
            !addr_ok(addr, (u32)n, 0))
            break;
        push(fs_write((const char *)(u32)name, (int)u,
                      (const char *)(u32)addr, (int)n));
        break;
    }
    case P_DELFILE: {                   /* ( c-addr u -- ior ) */
        cell u = pop(), name = pop();

        if (!aborted && name_ok(name, u))
            push(fs_delete((const char *)(u32)name, (int)u));
        break;
    }
    case P_RENFILE: {                   /* ( c-addr1 u1 c-addr2 u2 -- ior ) */
        cell u2 = pop(), n2 = pop(), u1 = pop(), n1 = pop();

        if (!aborted && name_ok(n1, u1) && name_ok(n2, u2))
            push(fs_rename((const char *)(u32)n1, (int)u1,
                           (const char *)(u32)n2, (int)u2));
        break;
    }
    case P_FILEQ: {                     /* ( c-addr u -- size vol -1 | 0 ) */
        cell u = pop(), name = pop();
        int vol, size;

        if (aborted || !name_ok(name, u))
            break;
        size = fs_stat((const char *)(u32)name, (int)u, &vol);
        if (size >= 0) {
            push(size);
            push(vol);
            push(-1);
        } else {
            push(0);
        }
        break;
    }
    case P_NFILES:  push(fs_count()); break;
    case P_FILENTH: {                   /* ( i -- c-addr u size vol ) */
        static char name[FS_NAME_MAX + 1];
        int size = 0, vol = 0, n = fs_entry((int)pop(), name, &size, &vol);

        if (n < 0) {
            error("no such directory entry", 0, 0);
            break;
        }
        push((cell)(u32)name);
        push(n);
        push(size);
        push(vol);
        break;
    }
    case P_DISKFREE:  push(fs_free_bytes()); break;
    case P_DISKSTATE: push(fs_state()); break;
    case P_FORMAT:    push(fs_format()); break;
    case P_MOUNT:     push(fs_mount()); break;
    case P_DOTIOR:    a = pop(); if (a) con_printf("%s\n", fs_error((int)a)); break;
    case P_DOTVOL:    con_puts(fs_volume_name((int)pop())); break;
    case P_UNUSED:    push((cell)(u32)(dict_end - dp)); break;
    case P_KEY: {                       /* ( -- c ) wait for a key */
        int c;

        while (!(c = input_getchar())) {
            vi_wait_vblank();
            input_poll();
            kernel_status_bar();
        }
        push(c);
        break;
    }
    case P_INKEY:   input_poll(); push(input_getchar()); break;
    case P_MS: {                        /* ( n -- ) in frames, a sixtieth */
        u32 until = vi_frames() + ((u32)pop() * 60u + 999u) / 1000u;

        while ((s32)(vi_frames() - until) < 0)
            vi_wait_vblank();
        break;
    }
    case P_CMOVE: {                     /* ( from to n -- ) */
        cell n = pop(), to = pop(), from = pop();

        if (n <= 0 || !addr_ok(from, (u32)n, 0) || !addr_ok(to, (u32)n, 0))
            break;
        {
            const u8 *f = (const u8 *)(u32)from;
            u8 *t = (u8 *)(u32)to;

            if (t < f)
                while (n--) *t++ = *f++;
            else
                while (n--) t[n] = f[n];
        }
        break;
    }
    case P_BEEP: {                      /* ( hz ms -- ) queue a note */
        cell ms = pop(), hz = pop();

        while (!aborted && !audio_note((int)hz, (int)ms))
            vi_wait_vblank();           /* the queue is full: let it drain */
        break;
    }
    case P_FINDNAME: {                  /* ( c-addr u -- xt | 0 ) */
        cell u = pop(), a = pop();

        if (!aborted && u > 0 && u <= NAME_MAX && addr_ok(a, (u32)u, 0))
            push(find((const char *)(u32)a, (int)u, 0));
        else if (!aborted)
            push(0);
        break;
    }
    case P_QUIET:    audio_quiet(); break;
    case P_SOUNDING: push(audio_busy() ? -1 : 0); break;
    case P_VOLUME:   audio_volume((int)pop()); break;
    case P_INCLUDE: case P_EDIT: case P_RUN:
        error("INCLUDE, EDIT and RUN are typed at the prompt", 0, 0);
        break;

    default:
        error("unimplemented primitive", 0, 0);
        break;
    }
}

void forth_execute(cell xt)
{
    cell *ip = 0;

    for (;;) {
        cell code = *(cell *)(u32)xt;

        if (code == P_DOCOL) {
            cell native = *(cell *)(u32)(xt + 4);

            if (native) {
                cell *(*fn)(cell *) = (cell *(*)(cell *))(u32)native;

                dsp = (int)(fn(&dstack[dsp]) - dstack);
                if (aborted)
                    return;
            } else {
                rpush((cell)(u32)ip);
                ip = (cell *)(u32)(xt + 8);
            }
        } else {
            prim((int)code, xt, &ip);
            if (aborted)
                return;
        }
        if (!ip)
            return;
        xt = *ip++;
    }
}

/* --------------------------------------------------- compiling words */

static cell xt_of(const char *name)
{
    return find(name, (int)slen(name), 0);
}

/* Shared by ." and S": read the string up to the closing quote, then either
 * compile it inline after its runtime, or act on it now.  S" at the prompt
 * rotates through four buffers, so S" OLD" S" NEW" RENAME-FILE gets two
 * different names. */
#define STRPADS 4
static char strpads[STRPADS][256];
static int strpad_next;

static void compile_string(int runtime)
{
    const char *s;
    int len, i;

    while (in_p < in_end && *in_p == ' ')
        in_p++;
    s = in_p;
    while (in_p < in_end && *in_p != '"')
        in_p++;
    len = (int)(in_p - s);
    if (in_p < in_end)
        in_p++;                 /* eat the closing quote */

    if (state) {
        if (!room(len + 8))
            return;
        comma(xt_of(runtime == P_SQRUN ? "(S\")" : "(.\")"));
        comma(len);
        for (i = 0; i < len; i++)
            *dp++ = (u8)s[i];
        dp = align_up(dp);
        return;
    }
    if (runtime == P_SQRUN) {
        char *strpad = strpads[strpad_next];

        strpad_next = (strpad_next + 1) % STRPADS;
        if (len > 256)
            len = 256;
        for (i = 0; i < len; i++)
            strpad[i] = s[i];
        push((cell)(u32)strpad);
        push(len);
        return;
    }
    for (i = 0; i < len; i++)
        con_putc(s[i]);
}

/* The loop just closed: its LEAVEs jump here. */
static void resolve_leaves(void)
{
    while (nleaves > 0) {
        cell slot = leaves[--nleaves];

        if (!slot)
            break;
        *(cell *)(u32)slot = (cell)(u32)dp;
    }
}

static void do_immediate_word(int code)
{
    switch (code) {
    case P_COLON: {
        cell prev = latest, h;

        if (state) {
            error("already compiling", 0, 0);
            return;
        }
        if (!next_word()) {
            error("name expected", 0, 0);
            return;
        }
        h = header(word_start, word_len, 0);
        if (!h)
            return;
        comma(P_DOCOL);
        comma(0);                       /* room for the compiled code */
        def_prev_latest = prev;
        def_header = h;
        csp = 0;
        nleaves = 0;
        state = 1;
        break;
    }
    case P_SEMI:
        if (!state) {
            error("not compiling", 0, 0);
            return;
        }
        if (csp) {
            error("unfinished IF/BEGIN/DO in definition", 0, 0);
            return;
        }
        comma(xt_of("EXIT"));
        {   /* Now that the thread is whole, try to compile it. */
            extern int native_compile(cell xt, cell thread_end);
            cell h = def_header;

            def_header = 0;
            state = 0;
#ifndef NO_NATIVE
            if (h)
                native_compile(name_to_xt(h), (cell)(u32)dp);
#endif
        }
        break;
    case P_IMMEDIATE:
        if (!latest)
            error("no word to make immediate", 0, 0);
        else
            *(u8 *)(u32)(latest + 4) |= IMMEDIATE;
        break;
    case P_VARIABLE:
        if (!next_word()) {
            error("name expected", 0, 0);
            return;
        }
        if (!header(word_start, word_len, 0))
            return;
        comma(P_DOVAR);
        comma(0);
        break;
    case P_CONSTANT:
        if (!next_word()) {
            error("name expected", 0, 0);
            return;
        }
        {
            cell v = pop();
            if (!header(word_start, word_len, 0))
                return;
            comma(P_DOCON);
            comma(v);
        }
        break;
    case P_LITERAL:
        comma(xt_of("(LIT)"));
        comma(pop());
        break;
    case P_IF:
        comma(xt_of("(0BRANCH)"));
        cpush((cell)(u32)dp);
        comma(0);
        break;
    case P_ELSE: {
        cell slot = cpop();
        if (aborted)
            break;
        comma(xt_of("(BRANCH)"));
        cpush((cell)(u32)dp);
        comma(0);
        *(cell *)(u32)slot = (cell)(u32)dp;
        break;
    }
    case P_THEN: {
        cell slot = cpop();
        if (!aborted)
            *(cell *)(u32)slot = (cell)(u32)dp;
        break;
    }
    case P_BEGIN:
        cpush((cell)(u32)dp);
        break;
    case P_UNTIL: {
        cell t = cpop();
        if (aborted)
            break;
        comma(xt_of("(0BRANCH)"));
        comma(t);
        break;
    }
    case P_AGAIN: {                     /* a loop with no way out but the */
        cell t = cpop();                /* break key: so it looks for it */
        if (aborted)
            break;
        comma(xt_of("(AGAIN)"));
        comma(t);
        break;
    }
    case P_WHILE:
        comma(xt_of("(0BRANCH)"));
        cpush((cell)(u32)dp);
        comma(0);
        break;
    case P_REPEAT: {
        cell slot = cpop(), top = cpop();
        if (aborted)
            break;
        comma(xt_of("(BRANCH)"));
        comma(top);
        *(cell *)(u32)slot = (cell)(u32)dp;
        break;
    }
    case P_DOIMM:
        comma(xt_of("(DO)"));
        cpush((cell)(u32)dp);
        if (nleaves < LEAVES_MAX)
            leaves[nleaves++] = 0;
        break;
    case P_LOOPIMM: {
        cell t = cpop();
        if (aborted)
            break;
        if (t == CS_QDO) {              /* ?DO: its skip lands after us */
            cell target = cpop(), slot = cpop();

            comma(xt_of("(LOOP)"));
            comma(target);
            *(cell *)(u32)slot = (cell)(u32)dp;
            resolve_leaves();
            break;
        }
        comma(xt_of("(LOOP)"));
        comma(t);
        resolve_leaves();
        break;
    }
    case P_QDOIMM:                      /* like DO, with a way round */
        comma(xt_of("(?DO)"));
        cpush((cell)(u32)dp);           /* the skip, resolved by LOOP */
        comma(0);
        cpush((cell)(u32)dp);
        cpush(CS_QDO);
        if (nleaves < LEAVES_MAX)
            leaves[nleaves++] = 0;
        break;
    case P_LEAVEIMM:
        if (!nleaves) {
            error("LEAVE outside DO ... LOOP", 0, 0);
            break;
        }
        comma(xt_of("(LEAVE)"));
        if (nleaves >= LEAVES_MAX) {
            error("too many LEAVEs", 0, 0);
            break;
        }
        leaves[nleaves++] = (cell)(u32)dp;
        comma(0);
        break;
    case P_PLOOPIMM: {
        cell t = cpop();

        if (aborted)
            break;
        if (t == CS_QDO) {
            cell target = cpop(), slot = cpop();

            comma(xt_of("(+LOOP)"));
            comma(target);
            *(cell *)(u32)slot = (cell)(u32)dp;
        } else {
            comma(xt_of("(+LOOP)"));
            comma(t);
        }
        resolve_leaves();
        break;
    }
    case P_DOES:
        if (!state) {
            error("DOES> belongs in a definition", 0, 0);
            break;
        }
        comma(xt_of("(DOES>)"));
        break;
    case P_RECURSE:
        if (!state || !def_header)
            error("RECURSE belongs in a definition", 0, 0);
        else
            comma(name_to_xt(def_header));
        break;
    case P_BRCHAR:
        if (!next_word()) {
            error("[CHAR] of what?", 0, 0);
            break;
        }
        comma(xt_of("(LIT)"));
        comma((u8)word_start[0]);
        break;
    case P_CASE:
        cpush(CS_CASE);
        break;
    case P_OF:                          /* OVER = IF DROP */
        comma(xt_of("OVER"));
        comma(xt_of("="));
        comma(xt_of("(0BRANCH)"));
        cpush((cell)(u32)dp);
        comma(0);
        comma(xt_of("DROP"));
        break;
    case P_ENDOF: {                     /* ELSE, keeping the exits stacked */
        cell slot = cpop();

        if (aborted)
            break;
        comma(xt_of("(BRANCH)"));
        {
            cell exit_slot = (cell)(u32)dp;

            comma(0);
            *(cell *)(u32)slot = (cell)(u32)dp;
            cpush(exit_slot);
        }
        break;
    }
    case P_ENDCASE: {
        cell slot;

        comma(xt_of("DROP"));
        while (!aborted && (slot = cpop()) != CS_CASE && !aborted)
            *(cell *)(u32)slot = (cell)(u32)dp;
        break;
    }
    case P_LBRACK:
        state = 0;
        break;
    case P_RBRACK:
        state = 1;
        break;
    case P_FORGET: {
        cell h;

        if (state) {
            error("cannot FORGET while compiling", 0, 0);
            return;
        }
        if (!next_word()) {
            error("name expected", 0, 0);
            return;
        }
        h = latest;
        while (h) {
            u8 len = *(u8 *)(u32)(h + 5);
            if (len == word_len && same((const char *)(u32)(h + 6),
                                        word_start, word_len))
                break;
            h = *(cell *)(u32)h;
        }
        if (!h)
            error("undefined", word_start, word_len);
        else if (h <= primitives_end)
            error("that word is part of the kernel", word_start, word_len);
        else {
            latest = *(cell *)(u32)h;
            dp = (u8 *)(u32)h;
        }
        break;
    }
    case P_DOSTR:
        compile_string(P_SLIT);
        break;
    case P_SQUOTE:
        compile_string(P_SQRUN);
        break;
    case P_DOTPAREN:                    /* .( says it now, even compiling */
        if (in_p < in_end && *in_p == ' ')
            in_p++;
        while (in_p < in_end && *in_p != ')')
            con_putc(*in_p++);
        if (in_p < in_end)
            in_p++;
        break;
    case P_PAREN:
        while (in_p < in_end && *in_p != ')')
            in_p++;
        if (in_p < in_end)
            in_p++;
        break;
    case P_BACKSLASH:
        while (in_p < in_end && *in_p != '\n')
            in_p++;
        break;
    case P_TICK:
        if (!next_word()) {
            error("name expected", 0, 0);
            return;
        }
        {
            cell xt = find(word_start, word_len, 0);
            if (!xt)
                error("undefined", word_start, word_len);
            else if (state) {
                comma(xt_of("(LIT)"));
                comma(xt);
            } else {
                push(xt);
            }
        }
        break;
    default:
        error("bad immediate", 0, 0);
        break;
    }
}

/* ---------------------------------------------------------- WORDS / SEE */

static void print_name(cell h)
{
    int len = *(u8 *)(u32)(h + 5);
    const char *s = (const char *)(u32)(h + 6);

    while (len--)
        con_putc(*s++);
}

static cell xt_to_header(cell xt)
{
    cell h = latest;

    while (h) {
        if (name_to_xt(h) == xt)
            return h;
        h = *(cell *)(u32)h;
    }
    return 0;
}

static void do_words(void)
{
    cell h = latest;
    int n = 0;

    while (h && n < 4096) {
        print_name(h);
        con_putc(' ');
        n++;
        h = *(cell *)(u32)h;
    }
    con_printf("\n%d words\n", n);
}

static void do_see(void)
{
    cell xt, h, *ip;
    int limit;

    if (!next_word()) {
        error("name expected", 0, 0);
        return;
    }
    xt = find(word_start, word_len, 0);
    if (!xt) {
        error("undefined", word_start, word_len);
        return;
    }
    h = xt_to_header(xt);
    if (*(cell *)(u32)xt != P_DOCOL) {
        con_puts("primitive ");
        print_name(h);
        con_putc('\n');
        return;
    }
    con_puts(": ");
    print_name(h);
    con_putc(' ');
    ip = (cell *)(u32)(xt + 8);
    for (limit = 0; limit < 512; limit++) {
        cell tok = *ip++;
        cell th = xt_to_header(tok);
        cell code = *(cell *)(u32)tok;

        if (code == P_EXIT) {
            con_puts(";\n");
            return;
        }
        if (code == P_LIT) {
            print_number(*ip++, base);
            continue;
        }
        if (code == P_SLIT) {
            cell len = *ip++;
            const char *s = (const char *)(u32)ip;
            int i;
            con_puts(".\" ");
            for (i = 0; i < len; i++)
                con_putc(s[i]);
            con_puts("\" ");
            ip += (len + 3) / 4;
            continue;
        }
        if (code == P_BRANCH || code == P_ZBRANCH || code == P_AGAINBR) {
            con_puts(code == P_BRANCH ? "(BRANCH)->" : code == P_AGAINBR
                     ? "(AGAIN)->" : "(0BRANCH)->");
            con_printf("%x ", (u32)*ip++);
            continue;
        }
        if (code == P_LOOP || code == P_PLOOP || code == P_QDO ||
            code == P_LEAVE) {
            con_puts(code == P_LOOP ? "LOOP " : code == P_PLOOP ? "+LOOP " :
                     code == P_QDO ? "?DO " : "LEAVE ");
            ip++;                       /* the target */
            continue;
        }
        if (code == P_SQRUN) {
            cell len = *ip++;
            const char *s = (const char *)(u32)ip;
            int i;
            con_puts("S\" ");
            for (i = 0; i < len; i++)
                con_putc(s[i]);
            con_puts("\" ");
            ip += (len + 3) / 4;
            continue;
        }
        if (code == P_PDOES) {
            con_puts("DOES> ");
            continue;
        }
        if (th)
            print_name(th);
        else
            con_printf("[%x]", (u32)tok);
        con_putc(' ');
    }
    con_puts("... (too long)\n");
}

static void do_dump(void)
{
    cell n = pop(), addr = pop();
    cell i;

    if (n < 0 || n > 4096 || !addr_ok(addr, (u32)n, 0))
        return;

    for (i = 0; i < n; i += 8) {
        cell j;
        con_printf("%08x  ", (u32)(addr + i));
        for (j = 0; j < 8; j++)
            con_printf("%02x ", *(u8 *)(u32)(addr + i + j));
        con_putc(' ');
        for (j = 0; j < 8; j++) {
            u8 c = *(u8 *)(u32)(addr + i + j);
            con_putc(c >= 32 && c < 127 ? (char)c : '.');
        }
        con_putc('\n');
    }
}

/* ------------------------------------------------------ INCLUDE / EDIT
 *
 * These two run other source, so they are only allowed where WORDS and SEE
 * are: at the top of a line, not inside a definition.  Nested source is
 * then just more lines, and an error in it cannot unwind the return stack
 * of a word that was halfway through running.
 */
#define INCLUDE_DEPTH 3
static char include_buf[INCLUDE_DEPTH][FS_FILE_MAX + 1];
static int include_depth;
static int line_errors;

int forth_include(const char *name, int len)
{
    char *buf;
    int n, before;

    if (include_depth >= INCLUDE_DEPTH) {
        error("files include each other too deeply", name, len);
        return 0;
    }
    buf = include_buf[include_depth];
    n = fs_read(name, len, buf, FS_FILE_MAX);
    if (n < 0) {
        error(fs_error(n), name, len);
        return 0;
    }
    buf[n] = 0;
    include_depth++;
    before = line_errors;
    forth_eval_lines(buf);
    include_depth--;
    if (line_errors != before) {
        error("errors in", name, len);
        return 0;
    }
    aborted = 0;
    return 1;
}

static void do_file_command(int code)
{
    char name[FS_NAME_MAX + 1];
    int i, len;

    if (!next_word()) {
        error(code == P_EDIT ? "EDIT what? EDIT NAME.FTH" :
              code == P_RUN  ? "RUN what? RUN NAME.FTH" :
                               "INCLUDE what? INCLUDE NAME.FTH", 0, 0);
        return;
    }
    if (!fs_name_ok(word_start, word_len)) {
        error(fs_error(FS_EBADNAME), word_start, word_len);
        return;
    }
    len = word_len;
    for (i = 0; i < len; i++)
        name[i] = word_start[i];
    name[len] = 0;
    if (code == P_INCLUDE) {
        forth_include(name, len);
        return;
    }
    if (code == P_RUN) {
        /* An app gets its window; the prompt comes back when it closes. */
        if (desktop_open_file(name))
            repl_repaint();
        else
            forth_include(name, len);
        return;
    }
    i = edit_file(name, len);
    repl_repaint();
    if (i == EDIT_RUN)
        forth_include(name, len);
}

/* --------------------------------------------------- outer interpreter */

static int is_immediate_code(int code)
{
    switch (code) {
    case P_COLON: case P_SEMI: case P_IMMEDIATE: case P_VARIABLE:
    case P_CONSTANT: case P_LITERAL: case P_IF: case P_ELSE: case P_THEN:
    case P_BEGIN: case P_UNTIL: case P_AGAIN: case P_WHILE: case P_REPEAT:
    case P_DOSTR: case P_PAREN: case P_BACKSLASH: case P_TICK:
    case P_DOIMM: case P_LOOPIMM: case P_FORGET:
    case P_LBRACK: case P_RBRACK: case P_SQUOTE:
    case P_QDOIMM: case P_PLOOPIMM: case P_DOES: case P_RECURSE:
    case P_LEAVEIMM: case P_DOTPAREN:
    case P_BRCHAR: case P_CASE: case P_OF: case P_ENDOF: case P_ENDCASE:
        return 1;
    default:
        return 0;
    }
}

void forth_eval(const char *src)
{
    const char *save_p = in_p, *save_end = in_end;

    if (!include_depth)
        interrupted = 0;
    in_p = src;
    in_end = src;
    while (*in_end)
        in_end++;
    aborted = 0;

    while (!aborted && next_word()) {
        int flags = 0;
        cell xt = find(word_start, word_len, &flags);
        cell num;

        if (xt) {
            int code = *(cell *)(u32)xt;

            if (is_immediate_code(code)) {
                do_immediate_word(code);
                continue;
            }
            if (code == P_WORDS && !state) {
                do_words();
                continue;
            }
            if (code == P_SEE && !state) {
                do_see();
                continue;
            }
            if (code == P_DUMP && !state) {
                do_dump();
                continue;
            }
            if ((code == P_INCLUDE || code == P_EDIT || code == P_RUN) &&
                !state) {
                do_file_command(code);
                continue;
            }
            if (state && !(flags & IMMEDIATE))
                comma(xt);
            else
                forth_execute(xt);
            continue;
        }
        if (to_number(word_start, word_len, &num)) {
            if (state) {
                comma(xt_of("(LIT)"));
                comma(num);
            } else {
                push(num);
            }
            continue;
        }
        error("undefined", word_start, word_len);
    }
    in_p = save_p;
    in_end = save_end;
}

/* Source is read a line at a time, the way a Forth reads a file: an error
 * abandons the line it was on and the system carries on with the next. */
void forth_eval_lines(const char *src)
{
    char buf[256];

    while (*src) {
        int n = 0;

        while (*src && *src != '\n') {
            if (n < (int)sizeof(buf) - 1)
                buf[n++] = *src;
            src++;
        }
        if (*src == '\n')
            src++;
        buf[n] = 0;
        if (!n)
            continue;
        forth_eval(buf);
        if (aborted) {
            u16 save = con_get_color();

            line_errors++;
            if (interrupted) {          /* the break key stops it all */
                con_color(save);
                return;
            }
            con_color(RGB(150, 110, 110));
            con_puts("   in: ");
            con_puts(buf);
            con_putc('\n');
            con_color(save);
        }
    }
}

int forth_depth(void)
{
    return dsp;
}

/* ------------------------------------------------------------- startup */

struct primdef {
    const char *name;
    unsigned char code;
    unsigned char flags;
};

static const struct primdef prims[] = {
    { "EXIT", P_EXIT, 0 }, { "(LIT)", P_LIT, 0 }, { "(.\")", P_SLIT, 0 },
    { ".\"", P_DOSTR, IMMEDIATE },
    { "(BRANCH)", P_BRANCH, 0 }, { "(0BRANCH)", P_ZBRANCH, 0 },
    { "(AGAIN)", P_AGAINBR, 0 },
    { "(DO)", P_DO, 0 }, { "(LOOP)", P_LOOP, 0 },
    { "DO", P_DOIMM, IMMEDIATE }, { "LOOP", P_LOOPIMM, IMMEDIATE },
    { "I", P_I, 0 }, { "J", P_J, 0 }, { "UNLOOP", P_UNLOOP, 0 },
    { "(LEAVE)", P_LEAVE, 0 }, { "LEAVE", P_LEAVEIMM, IMMEDIATE },
    { "DUP", P_DUP, 0 }, { "?DUP", P_QDUP, 0 }, { "DROP", P_DROP, 0 },
    { "2DUP", P_2DUP, 0 }, { "2DROP", P_2DROP, 0 }, { "2SWAP", P_2SWAP, 0 },
    { "SWAP", P_SWAP, 0 }, { "OVER", P_OVER, 0 }, { "ROT", P_ROT, 0 },
    { "NIP", P_NIP, 0 }, { "TUCK", P_TUCK, 0 }, { "PICK", P_PICK, 0 },
    { "DEPTH", P_DEPTH, 0 }, { ">R", P_TOR, 0 }, { "R>", P_RFROM, 0 },
    { "R@", P_RFETCH, 0 }, { "ROLL", P_ROLL, 0 },
    { "+", P_ADD, 0 }, { "-", P_SUB, 0 }, { "*", P_MUL, 0 },
    { "/", P_DIV, 0 }, { "MOD", P_MOD, 0 }, { "NEGATE", P_NEGATE, 0 },
    { "ABS", P_ABS, 0 }, { "MIN", P_MIN, 0 }, { "MAX", P_MAX, 0 },
    { "1+", P_1PLUS, 0 }, { "1-", P_1MINUS, 0 }, { "2*", P_2MUL, 0 },
    { "2/", P_2DIV, 0 },
    { "AND", P_AND, 0 }, { "OR", P_OR, 0 }, { "XOR", P_XOR, 0 },
    { "INVERT", P_INVERT, 0 }, { "LSHIFT", P_LSHIFT, 0 },
    { "RSHIFT", P_RSHIFT, 0 },
    { "=", P_EQ, 0 }, { "<>", P_NE, 0 }, { "<", P_LT, 0 }, { ">", P_GT, 0 },
    { "U<", P_ULT, 0 }, { "0=", P_ZEQ, 0 }, { "0<", P_ZLT, 0 },
    { "0>", P_ZGT, 0 },
    { "@", P_FETCH, 0 }, { "!", P_STORE, 0 }, { "C@", P_CFETCH, 0 },
    { "C!", P_CSTORE, 0 }, { "+!", P_PLUSSTORE, 0 }, { "FILL", P_FILL, 0 },
    { ".", P_DOT, 0 }, { "U.", P_UDOT, 0 }, { "H.", P_HDOT, 0 },
    { ".S", P_DOTS, 0 }, { "EMIT", P_EMIT, 0 }, { "CR", P_CR, 0 },
    { "SPACE", P_SPACE, 0 }, { "SPACES", P_SPACES, 0 }, { "TYPE", P_TYPE, 0 },
    { "HERE", P_HERE, 0 }, { "ALLOT", P_ALLOT, 0 }, { ",", P_COMMA, 0 },
    { "C,", P_CCOMMA, 0 }, { "CELLS", P_CELLS, 0 }, { "CELL+", P_CELLPLUS, 0 },
    { ":", P_COLON, IMMEDIATE }, { ";", P_SEMI, IMMEDIATE },
    { "IMMEDIATE", P_IMMEDIATE, IMMEDIATE },
    { "VARIABLE", P_VARIABLE, IMMEDIATE },
    { "CONSTANT", P_CONSTANT, IMMEDIATE },
    { "LITERAL", P_LITERAL, IMMEDIATE },
    { "[", P_LBRACK, IMMEDIATE }, { "]", P_RBRACK, IMMEDIATE },
    { "IF", P_IF, IMMEDIATE }, { "ELSE", P_ELSE, IMMEDIATE },
    { "THEN", P_THEN, IMMEDIATE }, { "BEGIN", P_BEGIN, IMMEDIATE },
    { "UNTIL", P_UNTIL, IMMEDIATE }, { "AGAIN", P_AGAIN, IMMEDIATE },
    { "WHILE", P_WHILE, IMMEDIATE }, { "REPEAT", P_REPEAT, IMMEDIATE },
    { "(", P_PAREN, IMMEDIATE }, { ".(", P_DOTPAREN, IMMEDIATE }, { "\\", P_BACKSLASH, IMMEDIATE },
    { "'", P_TICK, IMMEDIATE }, { "EXECUTE", P_EXECUTE, 0 },
    { "WORDS", P_WORDS, 0 }, { "SEE", P_SEE, 0 }, { "DUMP", P_DUMP, 0 },
    { "DECIMAL", P_DECIMAL, 0 }, { "HEX", P_HEX, 0 }, { "BASE", P_BASE, 0 },
    { "ABORT", P_ABORT, 0 }, { "FORGET", P_FORGET, IMMEDIATE },
    { "PAGE", P_PAGE, 0 }, { "AT", P_AT, 0 }, { "INK", P_INK, 0 },
    { "RGB", P_RGBW, 0 }, { "FRAMES", P_FRAMES, 0 }, { "VSYNC", P_VSYNC, 0 },
    { "REPORT", P_REPORT, 0 },
    { "FB", P_FB, 0 }, { "CLS", P_CLS, 0 }, { "PLOT", P_PLOT, 0 },
    { "BOX", P_BOX, 0 }, { "FRAME", P_FRAME, 0 },
    { "HLINE", P_HLINE, 0 }, { "VLINE", P_VLINE, 0 }, { "LINE", P_LINE, 0 },
    { "DRAW-TEXT", P_DRAWTEXT, 0 },
    { "BLIT", P_BLIT, 0 }, { "BLIT-SPRITE", P_BLITKEY, 0 },
    { "(S\")", P_SQRUN, 0 }, { "S\"", P_SQUOTE, IMMEDIATE },
    { "F*", P_FMUL, 0 }, { "F/", P_FDIV, 0 }, { "FSQRT", P_FSQRT, 0 },
    { "CANVAS-X", P_CANVASX, 0 }, { "CANVAS-Y", P_CANVASY, 0 },
    { "CANVAS-W", P_CANVASW, 0 }, { "CANVAS-H", P_CANVASH, 0 },
    { "KEY!", P_KEYSET, 0 }, { "MOUSE-X", P_MOUSEX, 0 },
    { "MOUSE-Y", P_MOUSEY, 0 }, { "MOUSE-B", P_MOUSEB, 0 },
    { "MOUSE-HIT", P_MOUSEHIT, 0 }, { "POLL", P_POLL, 0 },
    { "BUTTONS", P_BUTTONS, 0 }, { "PRESSED", P_PRESSED, 0 },
    { "TICKS", P_TICKS, 0 }, { "CURSOR", P_CURSOR, 0 },
    { "HIDE-CURSOR", P_HIDECUR, 0 },
    { "CLIP", P_CLIP, 0 }, { "NOCLIP", P_NOCLIP, 0 },
    { "SET-CANVAS", P_SETCANVAS, 0 },
    { "PARSE-NAME", P_PARSENAME, 0 }, { "INCLUDE", P_INCLUDE, 0 },
    { "EDIT", P_EDIT, 0 }, { "LOAD-FILE", P_LOADFILE, 0 },
    { "SAVE-FILE", P_SAVEFILE, 0 }, { "DELETE-FILE", P_DELFILE, 0 },
    { "RENAME-FILE", P_RENFILE, 0 }, { "FILE?", P_FILEQ, 0 },
    { "#FILES", P_NFILES, 0 }, { "FILE#", P_FILENTH, 0 },
    { "DISK-FREE", P_DISKFREE, 0 }, { "DISK-STATE", P_DISKSTATE, 0 },
    { "(FORMAT)", P_FORMAT, 0 }, { "MOUNT", P_MOUNT, 0 },
    { ".IOR", P_DOTIOR, 0 }, { ".VOL", P_DOTVOL, 0 },
    { "UNUSED", P_UNUSED, 0 }, { "KEY", P_KEY, 0 }, { "INKEY", P_INKEY, 0 },
    { "MS", P_MS, 0 }, { "CMOVE", P_CMOVE, 0 }, { "RUN", P_RUN, 0 },
    { "CREATE", P_CREATE, 0 }, { "(DOES>)", P_PDOES, 0 },
    { "DOES>", P_DOES, IMMEDIATE }, { ">BODY", P_TOBODY, 0 },
    { "(?DO)", P_QDO, 0 }, { "?DO", P_QDOIMM, IMMEDIATE },
    { "(+LOOP)", P_PLOOP, 0 }, { "+LOOP", P_PLOOPIMM, IMMEDIATE },
    { "RECURSE", P_RECURSE, IMMEDIATE }, { "CHAR", P_CHAR, 0 },
    { "[CHAR]", P_BRCHAR, IMMEDIATE }, { "CASE", P_CASE, IMMEDIATE },
    { "OF", P_OF, IMMEDIATE }, { "ENDOF", P_ENDOF, IMMEDIATE },
    { "ENDCASE", P_ENDCASE, IMMEDIATE }, { "ACCEPT", P_ACCEPT, 0 },
    { "FIND-NAME", P_FINDNAME, 0 }, { "BEEP", P_BEEP, 0 }, { "QUIET", P_QUIET, 0 },
    { "SOUNDING?", P_SOUNDING, 0 }, { "VOLUME", P_VOLUME, 0 },
};

void forth_init(void)
{
    unsigned i;

    dp = dict;
    latest = 0;
    dsp = rsp = csp = 0;
    def_header = 0;
    state = 0;
    base = 10;
    aborted = 0;
    for (i = 0; i < sizeof(prims) / sizeof(prims[0]); i++)
        defprim(prims[i].name, prims[i].code, prims[i].flags);
    primitives_end = latest;    /* FORGET stops here */
}

u32 forth_here(void)
{
    return (u32)dp;
}

u32 forth_dict_base(void)
{
    return (u32)dict;
}

/* An app compiles into the dictionary when its window opens and is rolled
 * back out of it when the window closes. */
/* Calling into Forth from C: the desktop drives an application a row at a
 * time, so the machine keeps reading its controller while a picture paints. */
void forth_push(cell v)
{
    push(v);
}

cell forth_pop(void)
{
    return pop();
}

int forth_call(const char *name)
{
    cell xt = find(name, slen(name), 0);

    if (!xt)
        return 0;
    aborted = 0;
    interrupted = 0;
    forth_execute(xt);
    return !aborted;
}

/* ------------------------------------------- what compiled code calls
 *
 * All of these take the Forth stack pointer and hand it back, which is the
 * calling convention src/native.c emits.
 */
cell *fs_prim(cell *stack, cell code)
{
    cell *ip = 0;

    tick();
    dsp = (int)(stack - dstack);
    prim((int)code, 0, &ip);
    return &dstack[dsp];
}

cell *fs_call(cell *stack, cell xt)
{
    dsp = (int)(stack - dstack);
    tick();
    forth_execute(xt);
    return &dstack[dsp];
}

cell *fs_type(cell *stack, cell addr, cell len)
{
    const char *s = (const char *)(u32)addr;

    while (len-- > 0)
        con_putc(*s++);
    return stack;
}

cell *fs_do(cell *stack)
{
    dsp = (int)(stack - dstack);
    {
        cell index = pop(), limit = pop();

        rpush(limit);
        rpush(index);
    }
    return &dstack[dsp];
}

cell *fs_index(cell *stack, cell level)
{
    dsp = (int)(stack - dstack);
    push(rstack[rsp - 1 - (level ? 2 : 0)]);
    return &dstack[dsp];
}

/* LOOP in compiled code.  Called through the fs_loop wrapper in entry.S,
 * which keeps $t8 -- compiled code's stack pointer -- safe across it. */
int fs_loop_c(void)
{
    cell index = rpop(), limit = rpop();

    index++;
    tick();
    if (index < limit && !aborted) {
        rpush(limit);
        rpush(index);
        return 1;
    }
    return 0;
}

/* What compiled AGAIN calls on its way round. */
cell *fs_tick(cell *stack)
{
    dsp = (int)(stack - dstack);
    tick();
    return &dstack[dsp];
}

cell *fs_bad_stack(cell *stack)
{
    dsp = (int)(stack - dstack);
    if (dsp < 0)
        dsp = 0;
    error("stack out of range in compiled code", 0, 0);
    return &dstack[dsp];
}

int fs_aborted(void)
{
    return aborted;
}

void forth_name_of(cell xt)
{
    cell h = xt_to_header(xt);

    if (h)
        print_name(h);
    else
        con_puts("?");
}

u32 forth_abort_flag(void)
{
    return (u32)&aborted;
}

u32 forth_rstack_base(void)
{
    return (u32)rstack;
}

u32 forth_rsp_addr(void)
{
    return (u32)&rsp;
}

u32 forth_stack_base(void)
{
    return (u32)dstack;
}

u32 forth_stack_top(void)
{
    return (u32)&dstack[DSTACK_MAX];
}

u32 forth_limit(void)
{
    return (u32)dict_end;
}

void forth_set_here(u32 where)
{
    dp = (u8 *)where;
}

u32 forth_mark(void)
{
    return (u32)latest;
}

void forth_release(u32 mark)
{
    cell h = latest;

    if (!mark)
        return;
    while (h && h != (cell)mark)
        h = *(cell *)(u32)h;
    if (h != (cell)mark)
        return;                             /* not ours to roll back */
    latest = (cell)mark;
    dp = align_up((u8 *)(u32)mark + 4);
    {   /* dp back to just past this word's own definition */
        u8 len = *(u8 *)(u32)(mark + 5);
        cell xt = (cell)(u32)align_up((u8 *)(u32)(mark + 6 + len));
        u8 *end = (u8 *)(u32)(xt + 4);

        if (*(cell *)(u32)xt == P_DOCOL) {
            cell *ip = (cell *)(u32)(xt + 8);
            int n;
            for (n = 0; n < 4096; n++) {
                cell tok = *ip++;
                if (*(cell *)(u32)tok == P_EXIT)
                    break;
                if (*(cell *)(u32)tok == P_LIT ||
                    *(cell *)(u32)tok == P_BRANCH ||
                    *(cell *)(u32)tok == P_ZBRANCH)
                    ip++;
                else if (*(cell *)(u32)tok == P_SLIT ||
                         *(cell *)(u32)tok == P_SQRUN) {
                    cell slen = *ip++;
                    ip += (slen + 3) / 4;
                }
            }
            end = (u8 *)ip;
        } else if (*(cell *)(u32)xt == P_DOVAR ||
                   *(cell *)(u32)xt == P_DOCON) {
            end = (u8 *)(u32)(xt + 8);
        }
        dp = align_up(end);
    }
}

u32 forth_dict_size(void)
{
    return DICT_BYTES;
}

u32 forth_word_count(void)
{
    cell h = latest;
    u32 n = 0;

    while (h) {
        n++;
        h = *(cell *)(u32)h;
    }
    return n;
}
