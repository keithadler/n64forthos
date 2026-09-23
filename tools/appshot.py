#!/usr/bin/env python3
"""Drive the desktop with the controller: open an app, run it, save a PNG.

    tools/appshot.py mandel --instr 200000000 --shot captures/mandel.png
"""
import argparse
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import n64emu                                    # noqa: E402
import screen                                    # noqa: E402

FAST_VI = 200              # a sped-up video clock, for the harness only
FRAME = FAST_VI * 525
PAD_A, PAD_B, PAD_DOWN, PAD_UP = 0x8000, 0x4000, 0x0400, 0x0800


def tap(m, mask, hold=3, gap=6):
    m.buttons(mask, True)
    m.run(m.icount + hold * FRAME)
    m.buttons(mask, False)
    m.run(m.icount + gap * FRAME)


def open_app(m, index):
    for _ in range(index):
        tap(m, PAD_DOWN)
    tap(m, PAD_A, gap=12)           # open: the window compiles the source
    m.run(m.icount + 60 * FRAME)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", choices=["mandel", "cornell"])
    ap.add_argument("--rom", default="build/n64forthos.z64")
    ap.add_argument("--instr", type=int, default=120_000_000)
    ap.add_argument("--shot", default=None)
    ap.add_argument("--text", action="store_true")
    args = ap.parse_args()

    m = n64emu.load(args.rom)
    m.run(16_000_000)
    open_app(m, 2 if args.app == "mandel" else 3)   # after Files and Console
    if args.text:
        for line in screen.decode(m.framebuffer()[2]):
            print(f"| {line}")
    tap(m, PAD_A, gap=1)            # run
    m.run(m.icount + args.instr)
    print(f"{m.icount} instructions, pc {m.pc:08x}")
    if args.shot:
        w, h = m.save_png(args.shot)
        print(f"{args.shot}: {w}x{h}")


if __name__ == "__main__":
    main()
