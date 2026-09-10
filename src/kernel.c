/* kernel.c -- bring the machine up, then hand it to Forth. */
#include "n64.h"
#include "system_fth.h"
#ifdef TEST_BUILD
#include "tests_fth.h"
#endif
#ifdef APP_DEBUG
#include "apps/mandel_fth.h"
#include "apps/cornell_fth.h"
#include "apps/navier_fth.h"
#endif

#define FB_PHYS   0x00200000u                   /* 2 MiB into RDRAM */
#define FB_ADDR   (0xA0000000u | FB_PHYS)       /* written uncached */

#define C_BG      RGB(10, 14, 30)
#define C_TEXT    RGB(205, 213, 228)
#define C_DIM     RGB(110, 125, 155)
#define C_CYAN    RGB(96, 224, 255)
#define C_AMBER   RGB(255, 190, 90)

extern u32 forth_here(void);
extern u32 forth_dict_base(void);
extern u32 forth_dict_size(void);
extern void _exc_entry(void);

u16 *fb_uncached(void)
{
    return (u16 *)FB_ADDR;
}

/* --------------------------------------------------------------- string */

static char *put_u32(char *p, u32 v, int base, int width, char pad)
{
    char tmp[12];
    int n = 0;

    do {
        u32 d = v % (u32)base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= (u32)base;
    } while (v);
    while (n < width)
        tmp[n++] = pad;
    while (n--)
        *p++ = tmp[n];
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

/* ------------------------------------------------------------ machine */

/* 4 MiB or 8 MiB?  Write high memory and see whether it sticks, and whether
 * it wrapped onto low memory on the way. */
u32 rdram_size(void)
{
    volatile u32 *lo = (volatile u32 *)0xA0000010u;
    volatile u32 *hi = (volatile u32 *)0xA0400010u;
    u32 slo = *lo, shi = *hi, size;

    *lo = 0x12345678u;
    *hi = 0x5A5AA5A5u;
    size = (*hi == 0x5A5AA5A5u && *lo == 0x12345678u) ? 8u : 4u;
    *lo = slo;
    *hi = shi;
    return size;
}

/* Point the exception vectors at _exc_entry.  Written through KSEG1 so the
 * instruction cache cannot be holding a stale copy. */
static void install_vectors(void)
{
    static const u32 vec[] = { 0x00000000u, 0x00000080u, 0x00000100u, 0x00000180u };
    u32 h = (u32)&_exc_entry;
    unsigned i;

    for (i = 0; i < sizeof(vec) / sizeof(vec[0]); i++) {
        volatile u32 *v = (volatile u32 *)(0xA0000000u | vec[i]);

        v[0] = 0x3C1A0000u | (h >> 16);         /* lui   k0, %hi(handler) */
        v[1] = 0x375A0000u | (h & 0xFFFFu);     /* ori   k0, k0, %lo(...) */
        v[2] = 0x03400008u;                     /* jr    k0               */
        v[3] = 0x00000000u;                     /* nop                    */
    }
}

static const char *exc_name(u32 cause)
{
    switch ((cause >> 2) & 0x1F) {
    case 0:  return "interrupt";
    case 2:  case 3:  return "TLB miss";
    case 4:  return "address error (load)";
    case 5:  return "address error (store)";
    case 6:  case 7:  return "bus error";
    case 8:  return "syscall";
    case 9:  return "breakpoint";
    case 10: return "reserved instruction";
    case 12: return "overflow";
    case 13: return "trap";
    default: return "exception";
    }
}

void exception_handler(u32 cause, u32 epc, u32 badvaddr)
{
    con_init(RGB(90, 0, 0));
    con_status(" n64forthos -- halted ");
    con_color(RGB(255, 220, 220));
    con_printf("\n  %s\n\n", exc_name(cause));
    con_printf("  Cause    %08x\n", cause);
    con_printf("  EPC      %08x\n", epc);
    con_printf("  BadVAddr %08x\n", badvaddr);
    con_puts("\n  The kernel stopped here.  Reset the console.\n");
    for (;;)
        vi_wait_vblank();
}

void panic(const char *msg)
{
    con_init(RGB(90, 0, 0));
    con_status(" n64forthos -- panic ");
    con_color(RGB(255, 220, 220));
    con_puts("\n  ");
    con_puts(msg);
    con_putc('\n');
    for (;;)
        vi_wait_vblank();
}

/* -------------------------------------------------------------- console */

static u32 word_count(void)
{
    /* WORDS counts by walking the dictionary; do it here without printing. */
    extern u32 forth_word_count(void);
    return forth_word_count();
}

static u32 ram_mb_cached = 4;

void kernel_status_bar(void)
{
    char buf[81];
    char *p = buf;
    u32 ram_mb = ram_mb_cached;
    u32 frames = vi_frames();

    p = put_str(p, " n64forthos 0.1   VR4300   640x480x16   RDRAM ");
    p = put_u32(p, ram_mb, 10, 0, ' ');
    p = put_str(p, "M   words ");
    p = put_u32(p, word_count(), 10, 0, ' ');
    p = put_str(p, "   frame ");
    p = put_u32(p, frames, 10, 5, '0');
    while (p < buf + 80)
        *p++ = ' ';
    *p = 0;
    con_status(buf);
}

/* The console has no keyboard yet, so the boot session is typed for it. */
static const char *const session[] = {
    "GREET",
    "SEE HELLO",
    "HELLO-WINDOW",
};

void kmain(void)
{
    u32 ram = rdram_size();
    unsigned i;

    ram_mb_cached = ram;
    vi_init((void *)FB_ADDR);
    install_vectors();
    con_init(C_BG);
    con_scroll_region(2, 19);

    con_color(C_CYAN);
    con_puts("n64forthos");
    con_color(C_DIM);
    con_puts("  --  a Forth system for the Nintendo 64\n\n");

    con_color(C_TEXT);
    con_printf("  VI     640x480 16bpp interlaced\n");
    con_printf("  FB     %08x, uncached\n", FB_ADDR);
    con_printf("  CPU    VR4300, caches invalidated\n");
    con_printf("  EXC    vectors at 80000000\n");
    con_printf("  RDRAM  %u MiB\n", ram);

    forth_init();
    con_printf("  DICT   %u KiB at %08x\n",
               forth_dict_size() >> 10, forth_dict_base());
    forth_eval_lines(system_fth);
    con_printf("  FORTH  %u words, boot source compiled\n\n", word_count());

#ifdef TEST_BUILD
    (void)i;
    con_color(C_TEXT);
    con_puts("running test/tests.fth\n");
    forth_eval_lines(tests_fth);
    con_color(C_DIM);
    con_puts("tests complete\n");
#else
    for (i = 0; i < sizeof(session) / sizeof(session[0]); i++) {
        con_color(C_DIM);
        con_puts("ok> ");
        con_color(C_AMBER);
        con_puts(session[i]);
        con_putc('\n');
        con_color(C_TEXT);
        forth_eval(session[i]);
        con_putc('\n');
    }
#endif

    con_color(C_TEXT);
#ifdef APP_DEBUG
    /* Development path: compile an app on the bare console, where its
     * errors are visible, and run it. */
#ifndef APP_DEBUG_SIZE
#define APP_DEBUG_SIZE 256
#endif
    forth_set_canvas(408, 64, APP_DEBUG_SIZE, APP_DEBUG_SIZE);
    con_puts("compiling app\n");
    forth_eval_lines(APP_DEBUG_SRC);
    con_printf("%u words\n", word_count());
    con_puts("running\n");
    {   /* drive it the way the desktop does: a row at a time */
        int rows = forth_call("ROWS") ? forth_pop() : 0;
        int row;

        forth_call("START");
        for (row = 0; row < rows; row++) {
            forth_push(row);
            if (!forth_call(APP_DEBUG_RUN))
                break;
        }
        con_printf("%u rows\n", (u32)rows);
    }
    con_puts("done\n");
    for (;;)
        vi_wait_vblank();
#else
    desktop_run();
#endif
}
