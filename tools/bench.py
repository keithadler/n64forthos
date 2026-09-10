#!/usr/bin/env python3
"""How fast does an app paint?  Boots the dev ROM and counts canvas pixels."""
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import n64emu

BG = (8, 8, 24)
CAN = (408, 64, 224, 224)


def painted(m):
    _, _, px = m.framebuffer()
    x0, y0, w, h = CAN
    n = 0
    for y in range(y0, min(y0 + h, len(px))):
        row = px[y]
        for x in range(x0, x0 + w):
            if row[x] != BG:
                n += 1
    return n


if __name__ == "__main__":
    rom = sys.argv[1] if len(sys.argv) > 1 else "build/n64forthos-dbg.z64"
    budget = int(sys.argv[2]) if len(sys.argv) > 2 else 30_000_000
    m = n64emu.load(rom, halfline=200)
    m.run(12_000_000)                       # boot and compile
    start = m.icount
    m.run(start + budget)
    n = painted(m)
    per = (budget / n) if n else 0
    print(f"{n} pixels in {budget/1e6:.0f}M instructions -> "
          f"{per:.0f} instructions/pixel")
    if n:
        full = per * CAN[2] * CAN[3]
        print(f"full canvas ~{full/1e6:.0f}M instructions, "
              f"~{full/93_750_000:.1f}s on hardware")
