#!/usr/bin/env python3
"""Lay out a .z64 cartridge image: header, IPL3 boot block, kernel.

    mkrom.py ipl3.bin kernel.bin out.z64 [NAME]

Layout
    0x0000  64-byte header (PI timing, boot address, CRCs, cart name)
    0x0040  the boot block the PIF copies into RSP DMEM and jumps to
    0x0FF8  payload length, read by that boot block
    0x1000  kernel image, loaded to 0x80000400
"""
import struct
import sys

BOOT_ADDR = 0x80000400
CRC_START = 0x1000
CRC_LEN = 0x100000
CIC_6102_SEED = 0xF8CA4DDC
CART_SIZE = 64 << 20        # 64 MiB of mask ROM, no chips on the board
M32 = 0xFFFFFFFF


def rol(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & M32 if n else v


def crcs(rom):
    """The CIC-6102 header checksum, over the megabyte after the boot block."""
    t1 = t2 = t3 = t4 = t5 = t6 = CIC_6102_SEED
    for i in range(0, CRC_LEN, 4):
        d = struct.unpack_from(">I", rom, CRC_START + i)[0]
        if ((t6 + d) & M32) < t6:
            t4 = (t4 + 1) & M32
        t6 = (t6 + d) & M32
        t3 ^= d
        r = rol(d, d & 0x1F)
        t5 = (t5 + r) & M32
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        b = struct.unpack_from(">I", rom, 0x750 + (i & 0xFF))[0]
        t1 = (t1 + (b ^ d)) & M32
    return (t6 ^ t4 ^ t3) & M32, (t5 ^ t2 ^ t1) & M32


def main():
    ipl3 = open(sys.argv[1], "rb").read()
    kernel = open(sys.argv[2], "rb").read()
    out = sys.argv[3]
    name = (sys.argv[4] if len(sys.argv) > 4 else "N64FORTHOS")[:20]

    if len(ipl3) > 0xFB8:
        raise SystemExit(f"boot block too big: {len(ipl3)} > 0xFB8 bytes")

    rom = bytearray()
    rom += struct.pack(">IIII", 0x80371240, 0x0000000F, BOOT_ADDR, 0x0000144C)
    rom += b"\0" * 8                    # CRC1, CRC2 -- filled in below
    rom += b"\0" * 8                    # reserved
    rom += name.ljust(20).encode("ascii")
    rom += b"\0" * 7                    # reserved
    rom += b"N"                         # category: N64 cartridge
    rom += b"FO"                        # cart id
    rom += b"E"                         # region: North America
    rom += bytes([0])                   # version
    assert len(rom) == 0x40

    rom += ipl3.ljust(0xFB8, b"\0")
    rom += struct.pack(">I", len(kernel))    # 0x0FF8: payload length
    rom += struct.pack(">I", 0)              # 0x0FFC: spare
    assert len(rom) == 0x1000

    rom += kernel
    # A 64 MiB cartridge -- the largest the PI's domain 1 addresses -- with
    # nothing on it but mask ROM: no save chip, no expansion, no coprocessor.
    if len(rom) > CART_SIZE:
        raise SystemExit(f"image is bigger than the cartridge: {len(rom)}")
    rom += b"\0" * (CART_SIZE - len(rom))

    crc1, crc2 = crcs(rom)
    struct.pack_into(">II", rom, 0x10, crc1, crc2)

    with open(out, "wb") as f:
        f.write(rom)
    print(f"{out}: {len(rom)//(1024*1024)} MiB cart  kernel {len(kernel)} B  "
          f"boot {len(ipl3)} B  CRC {crc1:08X} {crc2:08X}")


if __name__ == "__main__":
    main()
