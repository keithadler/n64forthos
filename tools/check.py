#!/usr/bin/env python3
"""Headless acceptance test: boot both cartridges and check what they did.

The test cartridge runs test/tests.fth and leaves pass/fail counts in RDRAM
where this can read them.  The normal cartridge is checked by decoding its
console back to text, so the assertions cover the whole path -- boot block,
kernel, Forth, console and video interface.
"""
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
    m = n64emu.load(rom)
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


def run_boot(rom="build/n64forthos.z64"):
    print(f"{rom}")
    m = n64emu.load(rom)
    m.run(6_000_000)
    check(m.vi[0] & 3 == 2, "VI is in 16-bit mode")
    check(m.vi[0] & 0x40 != 0, "VI is interlaced")
    check(m.vi[2] == 640, f"VI width is 640 (got {m.vi[2]})")
    w, h, px = m.framebuffer()
    # The framebuffer is 640x480; NTSC shows 474 lines of it, which is what
    # VI_V_START asks for and what a television would have displayed.
    check((w, h) == (640, 474), f"VI shows 640x474 of it (got {w}x{h})")

    lines = screen.decode(px)
    text = "\n".join(lines)
    check("n64forthos" in lines[0], "status bar names the system")
    check("Hello, World!" in text, "the console says Hello, World!")
    check("640x480 16bpp interlaced" in text, "boot log reports the video mode")
    check(any(l.strip() == "49" for l in lines), "7 SQUARE . printed 49")
    check("10 9 8 7 6 5 4 3 2 1" in text, "DO/LOOP counted down")
    check('": HELLO ." Hello, World!" CR ;"'.strip('"') in text,
          "SEE decompiled HELLO")
    check(text.count("?") - text.count("?DUP") == 0, "no errors on the screen")

    band = px[440]                      # the row of colour bars BARS painted
    check(len({p for p in band}) >= 5, "BARS painted the graphics band")
    check("a window drawn in Forth" in text, "HELLO-WINDOW drew its panel")
    m.save_png("captures/boot.png")
    return m


if __name__ == "__main__":
    run_tests()
    run_boot()
    print()
    if failures:
        print(f"{len(failures)} check(s) failed:")
        for f in failures:
            print(f"  - {f}")
        raise SystemExit(1)
    print("all checks passed")
