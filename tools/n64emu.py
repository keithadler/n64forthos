#!/usr/bin/env python3
"""A small VR4300 interpreter: enough of an N64 to boot this cartridge.

It exists to answer one question without a window or a real console: after
the PIF hands control to the boot block, what ends up in the framebuffer the
video interface is pointed at?  It boots the way the hardware does -- the
first 0x1000 bytes of the cartridge into RSP DMEM, PC at 0xA4000040 -- so the
boot block is under test too, not just the kernel.

Implemented: the integer MIPS III subset clang emits for this kernel, plus
the VI, PI and SP register blocks the kernel touches.  Not implemented: the
FPU, the TLB, the RSP/RDP, interrupts, cache timing.  Anything unimplemented
raises rather than silently doing the wrong thing.
"""
import struct
import sys

M32 = 0xFFFFFFFF
RDRAM_SIZE = 8 << 20
HALFLINES = 525                  # NTSC half-lines per field pair
CYCLES_PER_HALFLINE = 200        # instructions, not cycles: fast enough that
                                 # a vsync spin ends, slow enough to be a spin


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


class Fault(Exception):
    pass


class N64:
    def __init__(self, rom, ram_mb=4, trace=None):
        self.rom = rom
        self.ram = bytearray(RDRAM_SIZE)
        self.ram_limit = ram_mb << 20
        self.dmem = bytearray(0x1000)
        self.imem = bytearray(0x1000)
        self.vi = [0] * 16
        self.pi = [0] * 8
        self.si_dram = 0
        self.pif = bytearray(64)
        # Four joybus channels.  A BlueRetro adapter can present a
        # controller, a mouse or a keyboard on any of them, so this can too.
        self.pads = [
            {"kind": "pad", "buttons": 0, "x": 0, "y": 0},
            {"kind": "mouse", "buttons": 0, "dx": 0, "dy": 0},
            {"kind": "keyboard", "keys": []},
            None,
        ]
        self.reg = [0] * 32
        self.cp0 = [0] * 32
        self.hi = self.lo = 0
        self.pc = 0
        self.icount = 0
        self.trace = trace
        self.stopped = None

    # ------------------------------------------------------------ memory
    def _phys(self, addr):
        addr &= M32
        if 0x80000000 <= addr < 0xC0000000:
            return addr & 0x1FFFFFFF
        if addr < 0x80000000:
            return addr            # KUSEG used unmapped here; we never do
        raise Fault(f"unmapped address {addr:08x} at pc {self.pc:08x}")

    def read32(self, addr):
        p = self._phys(addr)
        if p < RDRAM_SIZE:
            if p >= self.ram_limit:
                return 0
            return struct.unpack_from(">I", self.ram, p)[0]
        if 0x04000000 <= p < 0x04001000:
            return struct.unpack_from(">I", self.dmem, p - 0x04000000)[0]
        if 0x04001000 <= p < 0x04002000:
            return struct.unpack_from(">I", self.imem, p - 0x04001000)[0]
        if 0x04040000 <= p < 0x04040020:            # SP status etc.
            return 1 if p == 0x04040010 else 0      # halted
        if 0x04400000 <= p < 0x04400040:
            idx = (p - 0x04400000) >> 2
            if idx == 4:                            # VI_CURRENT
                line = (self.icount // CYCLES_PER_HALFLINE) % HALFLINES
                return line << 1
            return self.vi[idx]
        if 0x04600000 <= p < 0x04600040:
            idx = (p - 0x04600000) >> 2
            return 0 if idx == 4 else self.pi[idx]  # PI_STATUS: never busy
        if 0x04800000 <= p < 0x04800020:            # SI: never busy
            return 0
        if 0x1FC007C0 <= p < 0x1FC00800:
            off = p - 0x1FC007C0
            return struct.unpack_from(">I", self.pif, off)[0]
        if 0x04300000 <= p < 0x04300010:            # MI
            return 0x01010101 if p == 0x0430000C else 0
        if 0x10000000 <= p < 0x10000000 + len(self.rom):
            off = p - 0x10000000
            return struct.unpack_from(">I", self.rom, off)[0]
        if 0x1FC00000 <= p < 0x1FC00800:            # PIF
            return 0
        raise Fault(f"read32 from unmapped {p:08x} (va) at pc {self.pc:08x}")

    def write32(self, addr, val):
        p = self._phys(addr)
        val &= M32
        if p < RDRAM_SIZE:
            if p < self.ram_limit:
                struct.pack_into(">I", self.ram, p, val)
            return
        if 0x04000000 <= p < 0x04001000:
            struct.pack_into(">I", self.dmem, p - 0x04000000, val)
            return
        if 0x04001000 <= p < 0x04002000:
            struct.pack_into(">I", self.imem, p - 0x04001000, val)
            return
        if 0x04400000 <= p < 0x04400040:
            self.vi[(p - 0x04400000) >> 2] = val
            return
        if 0x04600000 <= p < 0x04600040:
            idx = (p - 0x04600000) >> 2
            self.pi[idx] = val
            if idx == 3:                            # PI_WR_LEN: cart -> RDRAM
                self._pi_dma(val + 1)
            return
        if 0x04040000 <= p < 0x04040020 or 0x04300000 <= p < 0x04300010:
            return
        if 0x04800000 <= p < 0x04800020:
            idx = (p - 0x04800000) >> 2
            if idx == 0:                            # SI_DRAM_ADDR
                self.si_dram = val & 0x00FFFFFF
            elif idx == 1:                          # PIF -> RDRAM
                self.ram[self.si_dram:self.si_dram + 64] = self.pif
            elif idx == 4:                          # RDRAM -> PIF, then run
                self.pif[:] = self.ram[self.si_dram:self.si_dram + 64]
                self.joybus()
            return
        if 0x1FC007C0 <= p < 0x1FC00800:
            struct.pack_into(">I", self.pif, p - 0x1FC007C0, val)
            return
        if 0x1FC00000 <= p < 0x1FC00800:
            return
        raise Fault(f"write32 to unmapped {p:08x} at pc {self.pc:08x}")

    # ----------------------------------------------------------- joybus
    def joybus(self):
        """What the PIF does with a command block: walk the four channels,
        answer for whatever is plugged into each one."""
        b = self.pif
        i = 0
        channel = 0
        while i < 64 and channel < 4:
            cmd = b[i]
            if cmd == 0xFE:
                break
            if cmd == 0xFF:
                i += 1
                continue
            if cmd in (0x00, 0xFD):
                channel += 1
                i += 1
                continue
            tx = cmd & 0x3F
            rx = b[i + 1] & 0x3F
            op = b[i + 2] if tx else 0
            resp = i + 2 + tx
            pad = self.pads[channel] if channel < 4 else None
            kind = pad["kind"] if pad else None
            if pad is None:
                b[i + 1] |= 0x80                    # nothing on this channel
            elif op in (0x00, 0xFF) and rx >= 3:    # identify
                ident = {"pad": (0x05, 0x00, 0x02),
                         "mouse": (0x02, 0x00, 0x00),
                         "keyboard": (0x00, 0x02, 0x00)}[kind]
                b[resp + 0], b[resp + 1], b[resp + 2] = ident
            elif op == 0x01 and kind == "pad" and rx >= 4:
                b[resp + 0] = (pad["buttons"] >> 8) & 0xFF
                b[resp + 1] = pad["buttons"] & 0xFF
                b[resp + 2] = pad["x"] & 0xFF
                b[resp + 3] = pad["y"] & 0xFF
            elif op == 0x01 and kind == "mouse" and rx >= 4:
                b[resp + 0] = (pad["buttons"] >> 8) & 0xFF
                b[resp + 1] = pad["buttons"] & 0xFF
                b[resp + 2] = pad["dx"] & 0xFF      # relative, and consumed
                b[resp + 3] = pad["dy"] & 0xFF
                pad["dx"] = pad["dy"] = 0
            elif op == 0x13 and kind == "keyboard" and rx >= 7:
                keys = (pad["keys"] + [0, 0, 0])[:3]
                for k in range(3):
                    b[resp + k * 2] = (keys[k] >> 8) & 0xFF
                    b[resp + k * 2 + 1] = keys[k] & 0xFF
                b[resp + 6] = 0
            else:
                b[i + 1] |= 0x40                    # unsupported: time out
            i = resp + rx
            channel += 1

    def buttons(self, mask, down=True, pad=0):
        p = self.pads[pad]
        if p is None:
            return
        if down:
            p["buttons"] |= mask
        else:
            p["buttons"] &= ~mask

    def channel(self, index, kind):
        """Plug something into a channel: 'pad', 'mouse', 'keyboard', None."""
        if kind is None:
            self.pads[index] = None
        elif kind == "pad":
            self.pads[index] = {"kind": "pad", "buttons": 0, "x": 0, "y": 0}
        elif kind == "mouse":
            self.pads[index] = {"kind": "mouse", "buttons": 0, "dx": 0, "dy": 0}
        else:
            self.pads[index] = {"kind": "keyboard", "keys": []}

    def mouse_move(self, dx, dy, index=1):
        p = self.pads[index]
        if p and p["kind"] == "mouse":
            p["dx"] = max(-127, min(127, p["dx"] + dx))
            p["dy"] = max(-127, min(127, p["dy"] + dy))

    def mouse_button(self, mask, down=True, index=1):
        p = self.pads[index]
        if p and p["kind"] == "mouse":
            if down:
                p["buttons"] |= mask
            else:
                p["buttons"] &= ~mask

    def key(self, code, down=True, index=2):
        """Our emulated keyboard sends ASCII; a Randnet one would not."""
        p = self.pads[index]
        if not p or p["kind"] != "keyboard":
            return
        if isinstance(code, str):
            code = ord(code)
        if down:
            if code not in p["keys"]:
                p["keys"] = (p["keys"] + [code])[:3]
        elif code in p["keys"]:
            p["keys"].remove(code)

    def _pi_dma(self, length):
        dst = self.pi[0] & 0x00FFFFFF
        src = self.pi[1] & 0x1FFFFFFF
        off = src - 0x10000000
        chunk = self.rom[off:off + length]
        self.ram[dst:dst + len(chunk)] = chunk

    def read8(self, addr):
        w = self.read32(addr & ~3)
        return (w >> (8 * (3 - (addr & 3)))) & 0xFF

    def read16(self, addr):
        w = self.read32(addr & ~3)
        return (w >> (16 * (1 - ((addr >> 1) & 1)))) & 0xFFFF

    def write8(self, addr, val):
        sh = 8 * (3 - (addr & 3))
        w = self.read32(addr & ~3)
        self.write32(addr & ~3, (w & ~(0xFF << sh)) | ((val & 0xFF) << sh))

    def write16(self, addr, val):
        sh = 16 * (1 - ((addr >> 1) & 1))
        w = self.read32(addr & ~3)
        self.write32(addr & ~3, (w & ~(0xFFFF << sh)) | ((val & 0xFFFF) << sh))

    # -------------------------------------------------------------- boot
    def boot(self):
        """What the PIF does: boot block into DMEM, then jump to it."""
        self.dmem[0:0x1000] = self.rom[0:0x1000]
        self.reg[20] = 1                 # $s4: TV type (NTSC)
        self.reg[22] = 0x3F              # $s6: CIC seed
        self.reg[29] = 0xA4001FF0        # $sp in DMEM, as on hardware
        self.pc = 0xA4000040

    # ------------------------------------------------------------ execute
    def run(self, max_instr=40_000_000, stop_pc=None):
        reg = self.reg
        while self.icount < max_instr:
            pc = self.pc
            if stop_pc is not None and pc == stop_pc:
                self.stopped = "stop_pc"
                return
            instr = self.fetch(pc)
            self.pc = (pc + 4) & M32
            self.step(instr, pc)
            reg[0] = 0
            self.icount += 1
        self.stopped = "instruction budget"

    def fetch(self, pc):
        return self.read32(pc)

    def branch(self, target, pc):
        """Execute the delay slot, then take the branch."""
        delay = self.fetch((pc + 4) & M32)
        self.pc = (pc + 8) & M32
        self.step(delay, (pc + 4) & M32)
        self.reg[0] = 0
        self.icount += 1
        self.pc = target & M32

    def step(self, instr, pc):
        reg = self.reg
        op = instr >> 26
        rs = (instr >> 21) & 31
        rt = (instr >> 16) & 31
        rd = (instr >> 11) & 31
        sa = (instr >> 6) & 31
        imm = instr & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm

        if self.trace is not None:
            self.trace(self, pc, instr)

        if op == 0:                                  # SPECIAL
            fn = instr & 0x3F
            if fn == 0:   reg[rd] = (reg[rt] << sa) & M32
            elif fn == 2: reg[rd] = (reg[rt] & M32) >> sa
            elif fn == 3: reg[rd] = (s32(reg[rt]) >> sa) & M32
            elif fn == 4: reg[rd] = (reg[rt] << (reg[rs] & 31)) & M32
            elif fn == 6: reg[rd] = (reg[rt] & M32) >> (reg[rs] & 31)
            elif fn == 7: reg[rd] = (s32(reg[rt]) >> (reg[rs] & 31)) & M32
            elif fn == 8: self.branch(reg[rs], pc)                    # JR
            elif fn == 9:                                             # JALR
                target = reg[rs]
                reg[rd if rd else 31] = (pc + 8) & M32
                self.branch(target, pc)
            elif fn == 10:                                            # MOVZ
                if reg[rt] == 0: reg[rd] = reg[rs]
            elif fn == 11:                                            # MOVN
                if reg[rt] != 0: reg[rd] = reg[rs]
            elif fn == 13: raise Fault(f"break at {pc:08x}")
            elif fn == 15: pass                                       # SYNC
            elif fn == 16: reg[rd] = self.hi
            elif fn == 17: self.hi = reg[rs]
            elif fn == 18: reg[rd] = self.lo
            elif fn == 19: self.lo = reg[rs]
            elif fn == 24 or fn == 25:                                # MULT(U)
                a, b = (s32(reg[rs]), s32(reg[rt])) if fn == 24 else \
                       (reg[rs] & M32, reg[rt] & M32)
                p = (a * b) & 0xFFFFFFFFFFFFFFFF
                self.lo, self.hi = p & M32, (p >> 32) & M32
            elif fn == 26 or fn == 27:                                # DIV(U)
                if fn == 26:
                    a, b = s32(reg[rs]), s32(reg[rt])
                    if b == 0:
                        self.lo, self.hi = 0, a & M32
                    else:
                        q = abs(a) // abs(b)
                        q = -q if (a < 0) != (b < 0) else q
                        self.lo, self.hi = q & M32, (a - b * q) & M32
                else:
                    a, b = reg[rs] & M32, reg[rt] & M32
                    if b == 0:
                        self.lo, self.hi = M32, a
                    else:
                        self.lo, self.hi = (a // b) & M32, (a % b) & M32
            elif fn == 32 or fn == 33: reg[rd] = (reg[rs] + reg[rt]) & M32
            elif fn == 34 or fn == 35: reg[rd] = (reg[rs] - reg[rt]) & M32
            elif fn == 36: reg[rd] = reg[rs] & reg[rt]
            elif fn == 37: reg[rd] = reg[rs] | reg[rt]
            elif fn == 38: reg[rd] = reg[rs] ^ reg[rt]
            elif fn == 39: reg[rd] = (~(reg[rs] | reg[rt])) & M32
            elif fn == 42: reg[rd] = 1 if s32(reg[rs]) < s32(reg[rt]) else 0
            elif fn == 43: reg[rd] = 1 if (reg[rs] & M32) < (reg[rt] & M32) else 0
            elif fn == 52:                                            # TEQ
                if reg[rs] == reg[rt]:
                    raise Fault(f"trap (teq) at {pc:08x}")
            else:
                raise Fault(f"SPECIAL fn {fn} at {pc:08x} ({instr:08x})")
        elif op == 1:                                # REGIMM
            if rt in (0, 16):                                         # BLTZ(AL)
                if rt == 16: reg[31] = (pc + 8) & M32
                if s32(reg[rs]) < 0: self.branch(pc + 4 + (simm << 2), pc)
            elif rt in (1, 17):                                       # BGEZ(AL)
                if rt == 17: reg[31] = (pc + 8) & M32
                if s32(reg[rs]) >= 0: self.branch(pc + 4 + (simm << 2), pc)
            else:
                raise Fault(f"REGIMM rt {rt} at {pc:08x}")
        elif op == 2:                                                 # J
            self.branch(((pc + 4) & 0xF0000000) | ((instr & 0x3FFFFFF) << 2), pc)
        elif op == 3:                                                 # JAL
            reg[31] = (pc + 8) & M32
            self.branch(((pc + 4) & 0xF0000000) | ((instr & 0x3FFFFFF) << 2), pc)
        elif op == 4:
            if reg[rs] == reg[rt]: self.branch(pc + 4 + (simm << 2), pc)
        elif op == 5:
            if reg[rs] != reg[rt]: self.branch(pc + 4 + (simm << 2), pc)
        elif op == 6:
            if s32(reg[rs]) <= 0: self.branch(pc + 4 + (simm << 2), pc)
        elif op == 7:
            if s32(reg[rs]) > 0: self.branch(pc + 4 + (simm << 2), pc)
        elif op in (8, 9): reg[rt] = (reg[rs] + simm) & M32            # ADDI(U)
        elif op == 10: reg[rt] = 1 if s32(reg[rs]) < simm else 0
        elif op == 11: reg[rt] = 1 if (reg[rs] & M32) < (simm & M32) else 0
        elif op == 12: reg[rt] = reg[rs] & imm
        elif op == 13: reg[rt] = reg[rs] | imm
        elif op == 14: reg[rt] = reg[rs] ^ imm
        elif op == 15: reg[rt] = (imm << 16) & M32                     # LUI
        elif op == 16:                                                 # COP0
            if rs == 0:   reg[rt] = self.cp0[rd]                       # MFC0
            elif rs == 4: self.cp0[rd] = reg[rt] & M32                 # MTC0
            else: raise Fault(f"COP0 rs {rs} at {pc:08x}")
        elif op == 32:                                                 # LB
            v = self.read8(reg[rs] + simm)
            reg[rt] = (v - 256 if v & 0x80 else v) & M32
        elif op == 33:                                                 # LH
            v = self.read16(reg[rs] + simm)
            reg[rt] = (v - 65536 if v & 0x8000 else v) & M32
        elif op == 35: reg[rt] = self.read32(reg[rs] + simm)           # LW
        elif op == 36: reg[rt] = self.read8(reg[rs] + simm)            # LBU
        elif op == 37: reg[rt] = self.read16(reg[rs] + simm)           # LHU
        elif op == 40: self.write8(reg[rs] + simm, reg[rt])            # SB
        elif op == 41: self.write16(reg[rs] + simm, reg[rt])           # SH
        elif op == 43: self.write32(reg[rs] + simm, reg[rt])           # SW
        elif op == 47: pass                                            # CACHE
        elif op == 34 or op == 38:                                     # LWL / LWR
            addr = (reg[rs] + simm) & M32
            aligned = self.read32(addr & ~3)
            shift = (addr & 3) * 8
            if op == 34:
                reg[rt] = ((aligned << shift) | (reg[rt] & ((1 << shift) - 1))) & M32
            else:
                shift = 24 - shift
                mask = (M32 << (32 - shift)) & M32 if shift else 0
                reg[rt] = ((aligned >> shift) | (reg[rt] & mask)) & M32
        elif op == 42 or op == 46:                                     # SWL / SWR
            addr = (reg[rs] + simm) & M32
            aligned = self.read32(addr & ~3)
            shift = (addr & 3) * 8
            if op == 42:
                mask = (M32 << (32 - shift)) & M32 if shift else 0
                self.write32(addr & ~3, (aligned & mask) | ((reg[rt] & M32) >> shift))
            else:
                shift = 24 - shift
                mask = ((1 << shift) - 1) if shift else 0
                self.write32(addr & ~3, (aligned & mask) | ((reg[rt] << shift) & M32))
        else:
            raise Fault(f"opcode {op} at {pc:08x} ({instr:08x})")

    # ------------------------------------------------------------- video
    def framebuffer(self):
        """(width, height, [rgb tuples]) for whatever the VI is showing."""
        ctrl = self.vi[0]
        origin = self.vi[1] & 0x00FFFFFF
        width = self.vi[2]
        if (ctrl & 3) != 2:
            raise Fault(f"VI not in 16-bit mode (control {ctrl:08x})")
        v_start = (self.vi[10] >> 16) & 0x3FF
        v_end = self.vi[10] & 0x3FF
        y_scale = self.vi[13] & 0xFFF
        lines = int((v_end - v_start) / 2 * (y_scale / 1024.0))
        if ctrl & 0x40:                       # serrate: two fields interlaced
            lines *= 2
        height = min(max(lines, 1), 480)
        px = []
        for y in range(height):
            base = origin + y * width * 2
            row = []
            for x in range(width):
                p = struct.unpack_from(">H", self.ram, base + x * 2)[0]
                r = (p >> 11) & 31
                g = (p >> 6) & 31
                b = (p >> 1) & 31
                row.append((r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2))
            px.append(row)
        return width, height, px

    def save_png(self, path):
        from PIL import Image
        w, h, px = self.framebuffer()
        img = Image.new("RGB", (w, h))
        img.putdata([p for row in px for p in row])
        img.save(path)
        return w, h


def load(path, ram_mb=4):
    rom = open(path, "rb").read()
    if rom[:4] != b"\x80\x37\x12\x40":
        raise SystemExit("not a big-endian .z64 image")
    n64 = N64(rom, ram_mb=ram_mb)
    n64.boot()
    return n64


if __name__ == "__main__":
    m = load(sys.argv[1] if len(sys.argv) > 1 else "build/n64forthos.z64")
    m.run(int(sys.argv[2]) if len(sys.argv) > 2 else 8_000_000)
    print(f"stopped after {m.icount} instructions ({m.stopped}) pc={m.pc:08x}")
    print(f"VI control={m.vi[0]:08x} origin={m.vi[1]:08x} width={m.vi[2]}")
