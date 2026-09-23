/* fs.c -- files.
 *
 * Two volumes.  ROM holds the files the cartridge was built with -- the
 * applications' source, and a README -- and cannot be written.  PAK is the
 * Controller Pak, the 32 KiB of battery-backed memory in the back of the
 * controller, and is where everything you write is kept.  With no pak in
 * the controller the same file system runs on a RAM disk instead, and says
 * so, because what you write there is gone at power off.
 *
 * A name is looked up on PAK first and then ROM, so saving MANDEL.FTH after
 * editing it gives you your own Mandelbrot, and deleting it gives you the
 * original back.
 *
 * The format is this system's own, not the libultra note table: a pak
 * holding game saves has to be formatted before it can be used, and FORMAT
 * says so before it does it.
 *
 *   page 0      header: magic, checksum, generation, label; then the
 *               allocation table, a byte a page
 *   pages 1-3   the directory: 24 entries of 32 bytes
 *   pages 4-127 data, 256 bytes a page, chained through the table
 *
 * A save writes the new file's pages into free space first and only then
 * rewrites the header and directory, so pulling the pak half way through a
 * save loses the new version, not the old one -- except in the few
 * milliseconds the metadata itself is being written, which the checksum
 * catches.
 *
 * The whole volume is held in RAM (32 KiB of 4 MiB) and read from the pak
 * a block at a time as it is needed; writes go straight through.
 */
#include "n64.h"
#include "apps/mandel_fth.h"
#include "apps/cornell_fth.h"
#include "apps/navier_fth.h"
#include "apps/life_fth.h"
#include "apps/wm_fth.h"
#include "apps/readme_txt.h"
#include "apps/hello_fth.h"
#include "apps/sketch_fth.h"
#include "apps/music_fth.h"
#include "apps/tasks_fth.h"

#define VOL_BYTES   32768
#define PAGE        256
#define PAGES       (VOL_BYTES / PAGE)
#define BLOCK       32
#define BLOCKS      (VOL_BYTES / BLOCK)
#define META_PAGES  4
#define FAT_OFF     128
#define DIR_OFF     PAGE
#define DIR_ENTRY   32
#define NAME_OFF    0
#define SIZE_OFF    20
#define FIRST_OFF   22
#define SERIAL_OFF  24

#define FAT_FREE    0x00
#define FAT_END     0xFF
#define FAT_SYSTEM  0xFE

static const u8 magic[8] = { 'N', '4', 'T', 'H', 'F', 'S', 0, 1 };

static u8 vol[VOL_BYTES] __attribute__((aligned(8)));
static u8 loaded[BLOCKS];
static int state = FS_NONE;
static int on_pak;                      /* 0: the RAM disk */
static int fresh;                       /* a blank pak was formatted */

/* ------------------------------------------------------------- the ROM */

typedef struct {
    const char *name;
    const char *text;
} romfile_t;

static const romfile_t rom[] = {
    { "README.TXT", readme_txt },
    { "HELLO.FTH", hello_fth },
    { "SKETCH.FTH", sketch_fth },
    { "MUSIC.FTH", music_fth },
    { "MANDEL.FTH", mandel_fth },
    { "CORNELL.FTH", cornell_fth },
    { "NAVIER.FTH", navier_fth },
    { "LIFE.FTH", life_fth },
    { "WM.FTH", wm_fth },
    { "TASKS.FTH", tasks_fth },
};
#define NROM ((int)(sizeof(rom) / sizeof(rom[0])))

static int slen(const char *s)
{
    int n = 0;

    while (s[n])
        n++;
    return n;
}

static int upper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* Names compare without regard to case, as Forth words do. */
static int name_eq(const u8 *stored, const char *name, int len)
{
    int i;

    for (i = 0; i < len; i++)
        if (upper(stored[i]) != upper((u8)name[i]))
            return 0;
    return i == FS_NAME_MAX || stored[i] == 0;
}

int fs_name_ok(const char *name, int len)
{
    int i;

    if (len < 1 || len > FS_NAME_MAX)
        return 0;
    for (i = 0; i < len; i++)
        if ((u8)name[i] <= ' ' || (u8)name[i] > '~' || name[i] == ':')
            return 0;
    return 1;
}

/* ------------------------------------------------------ the pak blocks */

static int load_block(int b)
{
    int tries;

    if (loaded[b] || !on_pak)
        return 0;
    for (tries = 0; tries < 3; tries++)
        if (pak_read((u16)(b * BLOCK), vol + b * BLOCK) == 0) {
            loaded[b] = 1;
            return 0;
        }
    return -1;
}

static int load_range(int off, int len)
{
    int b;

    for (b = off / BLOCK; b <= (off + len - 1) / BLOCK; b++)
        if (load_block(b))
            return -1;
    return 0;
}

static int store_range(int off, int len)
{
    int b, tries;

    if (!on_pak)
        return 0;
    for (b = off / BLOCK; b <= (off + len - 1) / BLOCK; b++) {
        for (tries = 0; tries < 3; tries++)
            if (pak_write((u16)(b * BLOCK), vol + b * BLOCK) == 0)
                break;
        if (tries == 3)
            return -1;
        loaded[b] = 1;
    }
    return 0;
}

/* ------------------------------------------------------------ metadata */

static u16 get16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);  p[3] = (u8)v;
}

static u16 checksum(void)
{
    u32 sum = 0x4E34;
    int i;

    for (i = 16; i < META_PAGES * PAGE; i++)
        sum = (sum * 31 + vol[i]) & 0xFFFF;
    return (u16)sum;
}

static u8 *entry(int i) { return vol + DIR_OFF + i * DIR_ENTRY; }
static u8 *fat(void) { return vol + FAT_OFF; }

static int commit(void)
{
    put16(vol + 10, get16(vol + 10) + 1u);          /* generation */
    put16(vol + 8, checksum());
    return store_range(0, META_PAGES * PAGE) ? FS_EIO : 0;
}

/* After a failed write the RAM copy may say things the pak does not: read
 * the pak's own idea of itself back. */
static int resync(int err)
{
    int b;

    for (b = 0; b < META_PAGES * PAGE / BLOCK; b++)
        loaded[b] = 0;
    fs_mount();
    return err;
}

static void blank(const char *label)
{
    int i;

    for (i = 0; i < META_PAGES * PAGE; i++)
        vol[i] = 0;
    for (i = 0; i < 8; i++)
        vol[i] = magic[i];
    for (i = 0; label[i] && i < 16; i++)
        vol[16 + i] = (u8)label[i];
    for (i = 0; i < META_PAGES; i++)
        fat()[i] = FAT_SYSTEM;
}

/* Look at what is in the controller and decide what PAK means. */
int fs_mount(void)
{
    int i;

    for (i = 0; i < BLOCKS; i++)
        loaded[i] = 0;
    on_pak = pak_present();
    if (!on_pak) {
        /* A RAM disk: formatted fresh, unless it already holds files from
         * earlier in this session. */
        if (state != FS_RAM) {
            blank("RAM DISK");
            put16(vol + 8, checksum());
        }
        state = FS_RAM;
        return state;
    }
    if (load_range(0, META_PAGES * PAGE)) {
        state = FS_NOANSWER;
        return state;
    }
    for (i = 0; i < 8; i++)
        if (vol[i] != magic[i]) {
            int blank_pak = 1, k;

            /* Nothing at all in the header -- no Nintendo file system, no
             * ours -- is a pak with nothing on it to lose. */
            for (k = 0; k < META_PAGES * PAGE; k++)
                if (vol[k])
                    blank_pak = 0;
            if (blank_pak && fs_format() == 0) {
                fresh = 1;
                return state;
            }
            state = FS_UNFORMATTED;
            return state;
        }
    state = (get16(vol + 8) == checksum()) ? FS_PAK : FS_CORRUPT;
    return state;
}

int fs_state(void)
{
    return state;
}

/* Did the last mount find a blank pak and format it?  Asked once. */
int fs_formatted_blank(void)
{
    int f = fresh;

    fresh = 0;
    return f;
}

int fs_format(void)
{
    int i;

    if (!pak_present()) {
        on_pak = 0;
        blank("RAM DISK");
        put16(vol + 8, checksum());
        state = FS_RAM;
        return 0;
    }
    on_pak = 1;
    for (i = 0; i < BLOCKS; i++)
        loaded[i] = 1;                  /* nothing on it is worth reading */
    for (i = META_PAGES * PAGE; i < VOL_BYTES; i++)
        vol[i] = 0;
    blank("CONTROLLER PAK");
    if (commit()) {
        state = FS_NOANSWER;
        return FS_EIO;
    }
    state = FS_PAK;
    return 0;
}

/* Is the cache still the pak in the controller?  Someone may have swapped
 * paks, or written this one elsewhere, since we last looked: the header
 * block's magic and generation -- which every commit bumps -- say.  If they
 * differ, look again rather than write an old directory over a new pak. */
static void revalidate(void)
{
    u8 head[BLOCK];
    int i;

    if (state == FS_RAM) {
        if (pak_present())
            fs_mount();                 /* a pak has arrived */
        return;
    }
    if (!on_pak)
        return;
    if (pak_read(0, head) != 0) {
        fs_mount();
        return;
    }
    for (i = 0; i < 12; i++)
        if (head[i] != vol[i]) {
            fs_mount();
            return;
        }
}

static int writable(void)
{
    if (state == FS_PAK || state == FS_RAM)
        return 0;
    if (state == FS_UNFORMATTED)
        return FS_EUNFORMATTED;
    if (state == FS_CORRUPT)
        return FS_ECORRUPT;
    return FS_EIO;
}

static int dir_find(const char *name, int len)
{
    int i;

    if (state != FS_PAK && state != FS_RAM)
        return -1;
    for (i = 0; i < FS_DIR_MAX; i++)
        if (entry(i)[0] && name_eq(entry(i), name, len))
            return i;
    return -1;
}

static int rom_find(const char *name, int len)
{
    int i;

    for (i = 0; i < NROM; i++) {
        int n = slen(rom[i].name);

        if (n == len && name_eq((const u8 *)rom[i].name, name, len))
            return i;
    }
    return -1;
}

int fs_free_bytes(void)
{
    int i, n = 0;

    if (state != FS_PAK && state != FS_RAM)
        return 0;
    for (i = META_PAGES; i < PAGES; i++)
        if (fat()[i] == FAT_FREE)
            n++;
    return n * PAGE;
}

int fs_capacity(void)
{
    return (PAGES - META_PAGES) * PAGE;
}

/* ------------------------------------------------------------- reading */

int fs_stat(const char *name, int len, int *vol_out)
{
    int i;

    revalidate();
    i = dir_find(name, len);

    if (i >= 0) {
        if (vol_out)
            *vol_out = on_pak ? FS_VOL_PAK : FS_VOL_RAM;
        return get16(entry(i) + SIZE_OFF);
    }
    i = rom_find(name, len);
    if (i >= 0) {
        if (vol_out)
            *vol_out = FS_VOL_ROM;
        return slen(rom[i].text);
    }
    return FS_ENOENT;
}

/* Up to max bytes of the file; the count read.  A buffer smaller than the
 * file gets its beginning, as READ-FILE would -- FILE? has the whole size. */
int fs_read(const char *name, int len, char *buf, int max)
{
    int i, size, page, at = 0;

    revalidate();
    i = dir_find(name, len);
    if (max < 0)
        return FS_ETOOBIG;
    if (i < 0) {
        const char *t;

        i = rom_find(name, len);
        if (i < 0)
            return FS_ENOENT;
        for (t = rom[i].text; *t && at < max; t++)
            buf[at++] = *t;
        return at;
    }
    size = get16(entry(i) + SIZE_OFF);
    if (size > max)
        size = max;
    page = entry(i)[FIRST_OFF];
    while (at < size) {
        int n = size - at, k;

        if (page < META_PAGES || page >= PAGES)
            return FS_ECORRUPT;
        if (n > PAGE)
            n = PAGE;
        if (load_range(page * PAGE, n))
            return FS_EIO;
        for (k = 0; k < n; k++)
            buf[at++] = (char)vol[page * PAGE + k];
        page = fat()[page];
    }
    return size;
}

/* ------------------------------------------------------------- writing */

static void free_chain(int page)
{
    int guard = 0;

    while (page >= META_PAGES && page < PAGES && guard++ < PAGES) {
        int next = fat()[page];

        fat()[page] = FAT_FREE;
        page = next;
    }
}

int fs_write(const char *name, int len, const char *buf, int size)
{
    int i, need, got = 0, first = FAT_END, prev = -1, page, at = 0, err;
    int old;
    u8 *e;

    revalidate();
    if ((err = writable()))
        return err;
    if (!fs_name_ok(name, len))
        return FS_EBADNAME;
    if (size < 0 || size > fs_capacity())
        return FS_ETOOBIG;
    old = dir_find(name, len);
    i = old;
    if (i < 0)
        for (i = 0; i < FS_DIR_MAX && entry(i)[0]; i++)
            ;
    if (i >= FS_DIR_MAX)
        return FS_EDIRFULL;

    /* The replaced file's pages still count as used until the directory
     * says otherwise, which is what makes the save safe.  Only when the new
     * version fits in no other way are the old pages reused -- and then a
     * pak pulled mid-save can lose the file. */
    need = (size + PAGE - 1) / PAGE;
    if (need * PAGE > fs_free_bytes()) {
        int old_pages = 0, p = (old >= 0) ? entry(old)[FIRST_OFF] : FAT_END;

        while (p >= META_PAGES && p < PAGES && old_pages < PAGES) {
            old_pages++;
            p = fat()[p];
        }
        if (need * PAGE > fs_free_bytes() + old_pages * PAGE)
            return FS_EFULL;
        free_chain(entry(old)[FIRST_OFF]);
        entry(old)[FIRST_OFF] = FAT_END;
    }

    /* Claim pages, fill them, write them.  The table is only changed in
     * RAM until commit(). */
    for (page = META_PAGES; page < PAGES && got < need; page++) {
        int k, n;

        if (fat()[page] != FAT_FREE)
            continue;
        n = size - at;
        if (n > PAGE)
            n = PAGE;
        for (k = 0; k < PAGE; k++)
            vol[page * PAGE + k] = (u8)(k < n ? buf[at + k] : 0);
        at += n;
        if (store_range(page * PAGE, PAGE))
            return resync(FS_EIO);
        fat()[page] = FAT_END;
        if (prev < 0)
            first = page;
        else
            fat()[prev] = (u8)page;
        prev = page;
        got++;
    }

    if (old >= 0)
        free_chain(entry(old)[FIRST_OFF]);
    e = entry(i);
    for (at = 0; at < DIR_ENTRY; at++)
        e[at] = 0;
    for (at = 0; at < len; at++)
        e[NAME_OFF + at] = (u8)name[at];
    put16(e + SIZE_OFF, (u32)size);
    e[FIRST_OFF] = (u8)(need ? first : FAT_END);
    put32(e + SERIAL_OFF, get16(vol + 10) + 1u);
    return commit() ? resync(FS_EIO) : 0;
}

int fs_delete(const char *name, int len)
{
    int i, err;

    revalidate();
    if ((err = writable()))
        return rom_find(name, len) >= 0 ? FS_EREADONLY : err;
    i = dir_find(name, len);
    if (i < 0)
        return rom_find(name, len) >= 0 ? FS_EREADONLY : FS_ENOENT;
    free_chain(entry(i)[FIRST_OFF]);
    for (err = 0; err < DIR_ENTRY; err++)
        entry(i)[err] = 0;
    return commit() ? resync(FS_EIO) : 0;
}

int fs_rename(const char *from, int flen, const char *to, int tlen)
{
    int i, k, err;

    revalidate();
    if ((err = writable()))
        return err;
    if (!fs_name_ok(to, tlen))
        return FS_EBADNAME;
    i = dir_find(from, flen);
    if (i < 0)
        return rom_find(from, flen) >= 0 ? FS_EREADONLY : FS_ENOENT;
    k = dir_find(to, tlen);
    if (k >= 0 && k != i)
        return FS_EEXISTS;
    for (k = 0; k < FS_NAME_MAX; k++)
        entry(i)[NAME_OFF + k] = (u8)(k < tlen ? to[k] : 0);
    return commit() ? resync(FS_EIO) : 0;
}

/* ----------------------------------------------------------- directory
 *
 * One list for both volumes: the writable one first, then whatever of the
 * ROM is not hidden behind a file of the same name.
 */
int fs_pak_files(void)
{
    int i, n = 0;

    for (i = 0; i < FS_DIR_MAX; i++)
        if ((state == FS_PAK || state == FS_RAM) && entry(i)[0])
            n++;
    return n;
}

int fs_count(void)
{
    int i, n = 0;

    revalidate();
    for (i = 0; i < FS_DIR_MAX; i++)
        if ((state == FS_PAK || state == FS_RAM) && entry(i)[0])
            n++;
    for (i = 0; i < NROM; i++)
        if (dir_find(rom[i].name, slen(rom[i].name)) < 0)
            n++;
    return n;
}

/* Two names, without regard to case: <0, 0 or >0. */
static int name_cmp(const u8 *a, const u8 *b)
{
    int i;

    for (i = 0; i < FS_NAME_MAX; i++) {
        int x = upper(a[i]), y = upper(b[i]);

        if (x != y || !x)
            return x - y;
    }
    return 0;
}

int fs_entry(int n, char *name, int *size, int *vol_out)
{
    int i, k, order[FS_DIR_MAX], used = 0;

    /* Your files in order of name, then the ROM's in the order it lists
     * them, which puts the README first. */
    for (i = 0; i < FS_DIR_MAX; i++) {
        if (!((state == FS_PAK || state == FS_RAM) && entry(i)[0]))
            continue;
        for (k = used; k > 0 && name_cmp(entry(order[k - 1]), entry(i)) > 0; k--)
            order[k] = order[k - 1];
        order[k] = i;
        used++;
    }
    if (n < used) {
        i = order[n];
        for (k = 0; k < FS_NAME_MAX && entry(i)[k]; k++)
            name[k] = (char)entry(i)[k];
        name[k] = 0;
        *size = get16(entry(i) + SIZE_OFF);
        *vol_out = on_pak ? FS_VOL_PAK : FS_VOL_RAM;
        return k;
    }
    n -= used;
    for (i = 0; i < NROM; i++) {
        if (dir_find(rom[i].name, slen(rom[i].name)) >= 0)
            continue;
        if (n-- == 0) {
            for (k = 0; rom[i].name[k]; k++)
                name[k] = rom[i].name[k];
            name[k] = 0;
            *size = slen(rom[i].text);
            *vol_out = FS_VOL_ROM;
            return k;
        }
    }
    return -1;
}

const char *fs_error(int err)
{
    switch (err) {
    case 0:               return "ok";
    case FS_ENOENT:       return "no such file";
    case FS_EFULL:        return "not enough room on the volume";
    case FS_EDIRFULL:     return "the directory is full (24 files)";
    case FS_ETOOBIG:      return "file too big";
    case FS_EREADONLY:    return "that file is in ROM and cannot be changed";
    case FS_EBADNAME:     return "bad file name (1-19 characters, no spaces)";
    case FS_EEXISTS:      return "a file by that name exists";
    case FS_EIO:          return "the Controller Pak did not answer properly";
    case FS_EUNFORMATTED: return "the Controller Pak is not formatted: FORMAT";
    case FS_ECORRUPT:     return "the Controller Pak's directory is damaged";
    default:              return "file error";
    }
}

const char *fs_volume_name(int v)
{
    switch (v) {
    case FS_VOL_PAK: return "PAK";
    case FS_VOL_RAM: return "RAM";
    default:         return "ROM";
    }
}
