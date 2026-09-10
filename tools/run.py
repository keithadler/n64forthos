#!/usr/bin/env python3
"""Boot a cartridge in tools/n64emu.py and show what came up.

    tools/run.py build/n64forthos.z64 --shot captures/boot.png --text
"""
import argparse
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import n64emu                                    # noqa: E402
import screen                                    # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="?", default="build/n64forthos.z64")
    ap.add_argument("--shot", help="write the framebuffer here as a PNG")
    ap.add_argument("--instr", type=int, default=6_000_000)
    ap.add_argument("--ram", type=int, default=4, help="RDRAM in MiB")
    ap.add_argument("--text", action="store_true", help="decode the console")
    args = ap.parse_args()

    m = n64emu.load(args.rom, ram_mb=args.ram)
    m.run(args.instr)
    print(f"{args.rom}: {m.icount} instructions, pc {m.pc:08x} ({m.stopped})")
    print(f"VI control {m.vi[0]:08x}  origin {m.vi[1]:08x}  width {m.vi[2]}")

    if args.shot:
        w, h = m.save_png(args.shot)
        print(f"{args.shot}: {w}x{h}")
    if args.text:
        _, _, px = m.framebuffer()
        for line in screen.decode(px):
            print(f"| {line}")


if __name__ == "__main__":
    main()
