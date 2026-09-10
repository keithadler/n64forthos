#!/usr/bin/env python3
"""Headless acceptance test: boot both cartridges and check what they did.

The test cartridge runs test/tests.fth and leaves pass/fail counts in RDRAM
where this can read them.  The normal cartridge is checked by decoding its
console back to text, so the assertions cover the whole path -- boot block,
kernel, Forth, console and video interface.
"""
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import n64emu                                    # noqa: E402
import screen                                    # noqa: E402

REPORT_ADDR = 0x00380000
REPORT_MAGIC = 0x54535431
MIN_ASSERTIONS = 100

failures = []


def check(ok, what):
    print(f"  {'pass' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)


def run_tests(rom="build/n64forthos-test.z64"):
    print(f"{rom}")
    m = n64emu.load(rom, halfline=FAST_VI)
    passes = fails = None
    for _ in range(120):
        m.run(m.icount + 500_000)
        magic, p, f = struct.unpack_from(">III", m.ram, REPORT_ADDR)
        if magic == REPORT_MAGIC:
            passes, fails = p, f
            break
    check(passes is not None, "test cartridge reached REPORT")
    if passes is None:
        return m
    print(f"        {passes} assertions passed, {fails} failed, "
          f"{m.icount} instructions")
    check(fails == 0, f"no failing assertions (got {fails})")
    check(passes >= MIN_ASSERTIONS,
          f"at least {MIN_ASSERTIONS} assertions ran (got {passes})")

    lines = screen.decode(m.framebuffer()[2])
    check(any("tests failed: 0" in l for l in lines),
          "the console agrees: no failures")
    m.save_png("captures/tests.png")
    return m


# --------------------------------------------------------------- the keyboard
FAST_VI = 200              # a sped-up video clock, for the harness only
FRAME = FAST_VI * 525      # instructions in one of its frames
PAD_A, PAD_B, PAD_Z, PAD_START = 0x8000, 0x4000, 0x2000, 0x1000
PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT = 0x0800, 0x0400, 0x0200, 0x0100


def keymap():
    """The on-screen keyboard layout, read out of src/repl.c so there is one
    copy of it."""
    src = open("src/repl.c").read()
    block = re.search(r"keymap\[KB_ROWS\] = \{(.*?)\};", src, re.S).group(1)
    rows = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
    return [r.encode().decode("unicode_escape") for r in rows]


def tap(m, mask, hold=6, gap=10):
    """Hold long enough that a full repaint cannot swallow the press: the
    kernel only reads the controller between paints, like the hardware."""
    m.buttons(mask, True)
    m.run(m.icount + hold * FRAME)
    m.buttons(mask, False)
    m.run(m.icount + gap * FRAME)


def move_to(m, pos, target):
    """Walk the highlight to a cell, the short way round."""
    rows, cols = 6, 12
    dr = (target[0] - pos[0]) % rows
    for _ in range(min(dr, rows - dr)):
        tap(m, PAD_DOWN if dr <= rows - dr else PAD_UP)
    dc = (target[1] - pos[1]) % cols
    for _ in range(min(dc, cols - dc)):
        tap(m, PAD_RIGHT if dc <= cols - dc else PAD_LEFT)
    return target


def type_text(m, text, pos=(0, 0)):
    """Type a line the way a person would: move, press A, repeat."""
    grid = keymap()
    for ch in text.upper():
        if ch == " ":
            tap(m, PAD_Z)                       # Z is a space, no travelling
            continue
        best = None
        for r, row in enumerate(grid):
            for c, k in enumerate(row):
                if k != ch:
                    continue
                cost = min((r - pos[0]) % 6, (pos[0] - r) % 6) + \
                       min((c - pos[1]) % 12, (pos[1] - c) % 12)
                if best is None or cost < best[0]:
                    best = (cost, (r, c))
        if best is None:
            raise SystemExit(f"no key for {ch!r}")
        pos = move_to(m, pos, best[1])
        tap(m, PAD_A)
    return pos


def type_keys(m, text, frames=3):
    """Type on the emulated Randnet keyboard, which sends ASCII."""
    for ch in text:
        m.key(ch, True)
        m.run(m.icount + frames * FRAME)
        m.key(ch, False)
        m.run(m.icount + frames * FRAME)


def hold(m, mask, frames=40):
    """A press long enough to survive a row of rendering: one row of Forth
    is a couple of million instructions, and the kernel only reads the
    controller between rows."""
    tap(m, mask, hold=frames, gap=12)


def open_from_desktop(m, index):
    for _ in range(index):
        tap(m, PAD_DOWN)
    tap(m, PAD_A, gap=12)
    m.run(m.icount + 40 * FRAME)


def run_repl(rom="build/n64forthos.z64"):
    """The prompt, typed at twice: with a keyboard and with a controller."""
    print(f"{rom}  (the prompt)")
    m = n64emu.load(rom, halfline=FAST_VI)
    m.run(16_000_000)
    check(m.pads[1] is not None and m.pads[1]["kind"] == "mouse",
          "a mouse is on channel 2")
    check(m.pads[2] is not None and m.pads[2]["kind"] == "keyboard",
          "a keyboard is on channel 3")

    open_from_desktop(m, 4)                     # the Console
    lines = screen.decode(m.framebuffer()[2])
    check(any("d-pad or mouse" in l for l in lines), "the console is up")

    type_keys(m, "2 3 + .\r")
    m.run(m.icount + 20 * FRAME)
    lines = screen.decode(m.framebuffer()[2])
    check(any("ok> 2 3 + ." in l for l in lines), "the keyboard typed the line")
    check(any(l[:50].strip() == "5" for l in lines), "Forth answered 5")

    pos = type_text(m, "HELLO")                 # now on the on-screen keyboard
    tap(m, PAD_START, hold=3, gap=10)
    lines = screen.decode(m.framebuffer()[2])
    check(sum("Hello, World!" in l for l in lines) >= 1,
          "the controller typed HELLO and it ran")
    m.save_png("captures/repl.png")
    return m


def canvas_colours(px, x0=368, y0=64, w=256, h=256):
    seen = {}
    for y in range(y0, min(y0 + h, len(px))):
        for x in range(x0, x0 + w):
            p = px[y][x]
            seen[p] = seen.get(p, 0) + 1
    return seen


def run_desktop(rom="build/n64forthos.z64"):
    """Open both applications from the desktop and start them rendering."""
    print(f"{rom}  (the desktop and its apps)")
    m = n64emu.load(rom, halfline=FAST_VI)
    m.run(16_000_000)
    lines = screen.decode(m.framebuffer()[2])
    text = "\n".join(lines)
    check("Mandelbrot" in text, "the desktop lists Mandelbrot")
    check("Cornell box" in text, "the desktop lists the Cornell box")
    check("Console" in text, "the desktop lists the console")

    check("Navier-Stokes" in text, "the desktop lists the Navier-Stokes demo")
    check("Life" in text, "the desktop lists Life")
    check("Devices" in text, "the desktop lists the devices window")

    # A pointer: put it over the first row and click.
    m.mouse_move(0, 240 - 160)                  # from the middle of the screen
    m.run(m.icount + 4 * FRAME)
    m.mouse_button(0x8000, True)
    m.run(m.icount + 4 * FRAME)
    m.mouse_button(0x8000, False)
    m.run(m.icount + 40 * FRAME)
    lines = screen.decode(m.framebuffer()[2])
    text = "\n".join(lines)
    check("mandel.fth" in text, "the app window shows its Forth source")
    check("CONSTANT DEPTH" in text, "the source is the code that will run")
    check("RUN" in text and "CLOSE" in text,
          "the window offers RUN and CLOSE to a pointer")

    tap(m, PAD_A, gap=1)                        # run it
    m.run(m.icount + 60_000_000)
    seen = canvas_colours(m.framebuffer()[2])
    check(len(seen) >= 8, f"Mandelbrot painted the canvas ({len(seen)} colours)")
    lines = screen.decode(m.framebuffer()[2])
    check(any("row" in l and "of 128" in l for l in lines),
          "it reports progress while it paints")
    m.save_png("captures/app-mandel.png")

    hold(m, PAD_B)                              # stop the picture
    lines = screen.decode(m.framebuffer()[2])
    check(any("stopped" in l for l in lines),
          "B stops a render without closing the window")
    hold(m, PAD_B, frames=12)                   # close, back to the desktop
    tap(m, PAD_DOWN)
    tap(m, PAD_A, gap=20)                       # open the Cornell box
    m.run(m.icount + 80 * FRAME)
    lines = screen.decode(m.framebuffer()[2])
    text = "\n".join(lines)
    check("cornell.fth" in text, "the ray tracer shows its source too")
    check("VARIABLE RX" in text, "including the ray it traces")

    tap(m, PAD_A, gap=1)                        # trace it
    m.run(m.icount + 120_000_000)
    seen = canvas_colours(m.framebuffer()[2])
    reds = sum(n for (r, g, b), n in seen.items() if r > 90 and g < 90)
    greens = sum(n for (r, g, b), n in seen.items() if g > 90 and r < 90)
    check(reds > 200, f"the left wall came out red ({reds} pixels)")
    check(greens > 200, f"the right wall came out green ({greens} pixels)")
    m.save_png("captures/app-cornell.png")

    hold(m, PAD_B)                              # stop the trace
    hold(m, PAD_B, frames=12)                   # close the window
    tap(m, PAD_DOWN)
    tap(m, PAD_A, gap=20)                       # Life
    m.run(m.icount + 60 * FRAME)
    tap(m, PAD_A, gap=1)
    m.run(m.icount + 30_000_000)
    seen = canvas_colours(m.framebuffer()[2])
    check(len(seen) >= 2, "Life is alive on the canvas")
    lines = screen.decode(m.framebuffer()[2])
    check(any("of 64" in l or "pass" in l for l in lines),
          "and generating: it reports rows, then passes")
    return m


def run_boot(rom="build/n64forthos.z64"):
    """The console as it is at boot, before the desktop paints over it."""
    print(f"{rom}  (boot)")
    m = n64emu.load(rom, halfline=FAST_VI)
    first, snapshot = [], []
    for _ in range(30):                     # catch it mid-handover
        m.run(m.icount + 500_000)
        lines = screen.decode(m.framebuffer()[2])
        if any("Hello, World!" in l for l in lines):
            snapshot = lines
            first = first or lines
        elif snapshot:
            break                           # the desktop has taken the screen
    m.run(m.icount + 4_000_000)             # let the desktop settle
    check(m.vi[0] & 3 == 2, "VI is in 16-bit mode")
    check(m.vi[0] & 0x40 != 0, "VI is interlaced")
    check(m.vi[2] == 640, f"VI width is 640 (got {m.vi[2]})")
    w, h, px = m.framebuffer()
    # The framebuffer is 640x480; NTSC shows 474 lines of it, which is what
    # VI_V_START asks for and what a television would have displayed.
    check((w, h) == (640, 474), f"VI shows 640x474 of it (got {w}x{h})")

    lines = snapshot or screen.decode(px)
    text = "\n".join(lines)
    check("n64forthos" in screen.decode(px)[0], "status bar names the system")
    check("Hello, World!" in text, "the console says Hello, World!")
    check("640x480 16bpp interlaced" in "\n".join(first),
          "boot log reports the video mode")
    check('": HELLO ." Hello, World!" CR ;"'.strip('"') in text,
          "SEE decompiled HELLO")
    check(text.count("?") - text.count("?DUP") == 0, "no errors on the screen")
    check(any("Devices" in l for l in screen.decode(px)),
          "the desktop took over when the boot session finished")

    check("a window drawn" in text, "HELLO-WINDOW drew its panel")
    m.save_png("captures/desktop.png")
    return m


if __name__ == "__main__":
    run_tests()
    run_boot()
    run_repl()
    run_desktop()
    print()
    if failures:
        print(f"{len(failures)} check(s) failed:")
        for f in failures:
            print(f"  - {f}")
        raise SystemExit(1)
    print("all checks passed")
