#!/usr/bin/env python3
"""Where does the time actually go?

Boots a cartridge, samples the program counter, and attributes each sample to
the nearest symbol in the ELF.  Crude, but it is measurement rather than
opinion, and it is what stopped two "optimisations" that turned out to be
worth nothing.

    tools/profile.py build/n64forthos-dbg.z64 build/kernel-dbg.elf 20000000
"""
import subprocess
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import n64emu                                    # noqa: E402

NM = "/opt/homebrew/opt/llvm/bin/llvm-nm"


def symbols(elf):
    out = subprocess.run([NM, "-n", elf], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].lower() in "tt":
            syms.append((int(parts[0], 16), parts[2]))
    return syms


DICT_BASE = 0x80300000
DICT_END = DICT_BASE + (512 << 10)


def name_for(syms, pc):
    if DICT_BASE <= pc < DICT_END:
        return "(compiled Forth)"
    lo, hi = 0, len(syms) - 1
    best = "?"
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= pc:
            best = syms[mid][1]
            lo = mid + 1
        else:
            hi = mid - 1
    return best


def main():
    rom = sys.argv[1] if len(sys.argv) > 1 else "build/n64forthos-dbg.z64"
    elf = sys.argv[2] if len(sys.argv) > 2 else "build/kernel-dbg.elf"
    budget = int(sys.argv[3]) if len(sys.argv) > 3 else 20_000_000
    syms = symbols(elf)

    m = n64emu.load(rom)
    m.run(12_000_000)                            # boot and compile
    counts = {}
    step = 997                                   # a prime, to avoid lockstep
    for _ in range(budget // step):
        m.run(m.icount + step)
        n = name_for(syms, m.pc & 0xFFFFFFFF)
        counts[n] = counts.get(n, 0) + 1

    total = sum(counts.values())
    print(f"{total} samples")
    for name, n in sorted(counts.items(), key=lambda kv: -kv[1])[:14]:
        print(f"  {100.0 * n / total:5.1f}%  {name}")


if __name__ == "__main__":
    main()
