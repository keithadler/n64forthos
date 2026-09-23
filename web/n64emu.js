/* n64emu.js -- the same VR4300 interpreter as tools/n64emu.py, in the
 * browser, so the page can give the operating system a real mouse and a real
 * keyboard.
 *
 * It boots the way the console does: the first 0x1000 bytes of the cartridge
 * into RSP data memory, PC at 0xA4000040.  It implements the integer MIPS
 * subset this kernel uses and the VI, PI and SI registers it touches -- not
 * the FPU, the TLB, the RSP or the RDP.  Anything else throws.
 *
 * Memory is a Uint32Array indexed by word, holding values in the machine's
 * own big-endian order, so byte and halfword access is shifting rather than
 * byte swapping.
 */

export class N64 {
  // A stock console has 4 MiB; the Expansion Pak is not assumed.
  constructor(rom, ramMB = 4) {
    this.rom = rom;                       // Uint8Array
    this.ramWords = new Uint32Array((8 << 20) >> 2);
    this.ramLimit = ramMB << 20;
    this.dmem = new Uint32Array(0x1000 >> 2);
    this.imem = new Uint32Array(0x1000 >> 2);
    this.vi = new Uint32Array(16);
    this.pi = new Uint32Array(8);
    this.pif = new Uint8Array(64);
    // The audio interface: buffers handed to onAudio(samples, rate) as the
    // kernel queues them.  It holds two at a time; audioFull(), if the page
    // sets it, says whether real speakers are behind, and otherwise the
    // buffers are timed in emulated instructions.
    this.ai = new Uint32Array(8);
    this.aiQueue = [];
    this.onAudio = null;
    this.audioFull = null;
    this.siDram = 0;
    // The RDP, to the extent this kernel uses it: fill rectangles.
    this.dp = { start: 0, end: 0, colour: 0, img: 0, width: 640 };
    this.reg = new Int32Array(32);
    this.cp0 = new Int32Array(32);
    this.hi = 0; this.lo = 0;
    this.pc = 0;
    this.icount = 0;
    this.halted = null;
    // Channel 0 controller, 1 mouse, 2 keyboard: what a BlueRetro adapter
    // can present, and what the page has to offer.
    this.pad = { buttons: 0, x: 0, y: 0 };
    // A Controller Pak in the controller: 32 KiB, null when there is none.
    // onPakWrite is told after every write, so the page can keep it.
    this.pak = new Uint8Array(32768);
    this.onPakWrite = null;
    this.mouse = { buttons: 0, dx: 0, dy: 0 };
    this.mouseConnected = true;           // false: nothing on channel 2
    this.keys = [];
    // Typed text, a key at a time: each one is reported down for one poll
    // of the keyboard and up for the next, so nothing is lost however long
    // the kernel takes between polls.
    this.keyQueue = [];
    this.keyPhase = 0;
  }

  boot() {
    for (let i = 0; i < 0x1000; i += 4) this.dmem[i >> 2] = this.romWord(i);
    this.reg[20] = 1;                     // $s4: NTSC
    this.reg[22] = 0x3f;                  // $s6: CIC seed
    this.reg[29] = 0xa4001ff0 | 0;        // $sp in DMEM, as on hardware
    this.pc = 0xa4000040 | 0;
  }

  romWord(off) {
    const r = this.rom;
    return ((r[off] << 24) | (r[off + 1] << 16) | (r[off + 2] << 8) | r[off + 3]) >>> 0;
  }

  /* ------------------------------------------------------------ memory */
  read32(addr) {
    addr = addr >>> 0;
    const p = (addr >= 0x80000000) ? (addr & 0x1fffffff) >>> 0 : addr;
    if (p < 0x00800000) {
      if (p >= this.ramLimit) return 0;
      return this.ramWords[p >>> 2];
    }
    if (p >= 0x04000000 && p < 0x04001000) return this.dmem[(p - 0x04000000) >>> 2];
    if (p >= 0x04001000 && p < 0x04002000) return this.imem[(p - 0x04001000) >>> 2];
    if (p >= 0x04040000 && p < 0x04040020) return p === 0x04040010 ? 1 : 0;
    if (p >= 0x04400000 && p < 0x04400040) {
      const idx = (p - 0x04400000) >> 2;
      if (idx === 4) {                    // VI_CURRENT, in half-lines
        // 93.75M instructions a second, 60 fields a second, 525 half-lines
        return (((this.icount / 2976) | 0) % 525) << 1;
      }
      return this.vi[idx];
    }
    if (p >= 0x04600000 && p < 0x04600040) {
      const idx = (p - 0x04600000) >> 2;
      return idx === 4 ? 0 : this.pi[idx];
    }
    if (p >= 0x04800000 && p < 0x04800020) return 0;
    if (p >= 0x04500000 && p < 0x04500018) {
      const idx = (p - 0x04500000) >> 2;
      if (idx !== 3) return this.ai[idx];
      while (this.aiQueue.length && this.aiQueue[0] <= this.icount) this.aiQueue.shift();
      const full = this.audioFull ? this.audioFull() : this.aiQueue.length >= 2;
      return ((full ? 0x80000000 : 0) | (this.aiQueue.length ? 0x40000000 : 0)) >>> 0;
    }
    if (p >= 0x04100000 && p < 0x04100020) {
      const idx = (p - 0x04100000) >> 2;
      return idx === 0 ? this.dp.start : (idx === 1 || idx === 2) ? this.dp.end : 0;
    }
    if (p >= 0x04300000 && p < 0x04300010) return p === 0x0430000c ? 0x01010101 : 0;
    if (p >= 0x1fc007c0 && p < 0x1fc00800) {
      const o = p - 0x1fc007c0, b = this.pif;
      return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
    }
    if (p >= 0x10000000 && p < 0x10000000 + this.rom.length) {
      return this.romWord(p - 0x10000000);
    }
    if (p >= 0x1fc00000 && p < 0x1fc00800) return 0;
    throw new Error(`read32 ${p.toString(16)} at pc ${(this.pc >>> 0).toString(16)}`);
  }

  write32(addr, val) {
    addr = addr >>> 0; val = val >>> 0;
    const p = (addr >= 0x80000000) ? (addr & 0x1fffffff) >>> 0 : addr;
    if (p < 0x00800000) {
      if (p < this.ramLimit) this.ramWords[p >>> 2] = val;
      return;
    }
    if (p >= 0x04000000 && p < 0x04001000) { this.dmem[(p - 0x04000000) >>> 2] = val; return; }
    if (p >= 0x04001000 && p < 0x04002000) { this.imem[(p - 0x04001000) >>> 2] = val; return; }
    if (p >= 0x04400000 && p < 0x04400040) { this.vi[(p - 0x04400000) >> 2] = val; return; }
    if (p >= 0x04600000 && p < 0x04600040) {
      const idx = (p - 0x04600000) >> 2;
      this.pi[idx] = val;
      if (idx === 3) this.piDma(val + 1);
      return;
    }
    if (p >= 0x04100000 && p < 0x04100020) {
      const idx = (p - 0x04100000) >> 2;
      if (idx === 0) this.dp.start = val;
      else if (idx === 1) { this.dp.end = val; this.rdpRun(); }
      return;
    }
    if (p >= 0x04500000 && p < 0x04500018) {
      const idx = (p - 0x04500000) >> 2;
      this.ai[idx] = val;
      if (idx === 1) this.aiDma(val & 0x3fff8);
      return;
    }
    if (p >= 0x04800000 && p < 0x04800020) {
      const idx = (p - 0x04800000) >> 2;
      if (idx === 0) this.siDram = val & 0x00ffffff;
      else if (idx === 1) this.pifToRam();
      else if (idx === 4) { this.ramToPif(); this.joybus(); }
      return;
    }
    if (p >= 0x1fc007c0 && p < 0x1fc00800) {
      const o = p - 0x1fc007c0, b = this.pif;
      b[o] = val >>> 24; b[o + 1] = (val >>> 16) & 255;
      b[o + 2] = (val >>> 8) & 255; b[o + 3] = val & 255;
      return;
    }
    if ((p >= 0x04040000 && p < 0x04040020) || (p >= 0x04300000 && p < 0x04300010)) return;
    if (p >= 0x1fc00000 && p < 0x1fc00800) return;
    throw new Error(`write32 ${p.toString(16)} at pc ${(this.pc >>> 0).toString(16)}`);
  }

  read8(a) { return (this.read32(a & ~3) >>> (8 * (3 - (a & 3)))) & 0xff; }
  read16(a) { return (this.read32(a & ~3) >>> (16 * (1 - ((a >> 1) & 1)))) & 0xffff; }
  write8(a, v) {
    const sh = 8 * (3 - (a & 3)), w = this.read32(a & ~3);
    this.write32(a & ~3, (w & ~(0xff << sh)) | ((v & 0xff) << sh));
  }
  write16(a, v) {
    const sh = 16 * (1 - ((a >> 1) & 1)), w = this.read32(a & ~3);
    this.write32(a & ~3, (w & ~(0xffff << sh)) | ((v & 0xffff) << sh));
  }

  piDma(length) {
    const dst = this.pi[0] & 0x00ffffff, src = (this.pi[1] & 0x1fffffff) - 0x10000000;
    for (let i = 0; i + 3 < length; i += 4) {
      this.ramWords[(dst + i) >>> 2] = this.romWord(src + i);
    }
  }

  ramToPif() {
    for (let i = 0; i < 64; i += 4) {
      const w = this.ramWords[(this.siDram + i) >>> 2];
      this.pif[i] = w >>> 24; this.pif[i + 1] = (w >>> 16) & 255;
      this.pif[i + 2] = (w >>> 8) & 255; this.pif[i + 3] = w & 255;
    }
  }

  pifToRam() {
    for (let i = 0; i < 64; i += 4) {
      const b = this.pif;
      this.ramWords[(this.siDram + i) >>> 2] =
        ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
    }
  }

  /* ---------------------------------------------------------- audio */
  aiDma(len) {
    const rate = 48681812 / ((this.ai[4] & 0x3fff) + 1);
    const frames = len >> 2;
    const start = Math.max(this.icount, this.aiQueue.length ? this.aiQueue[this.aiQueue.length - 1] : 0);
    this.aiQueue.push(start + Math.round(frames / rate * 93.75e6));
    if (!this.onAudio) return;
    const samples = new Int16Array(frames * 2), base = this.ai[0] & 0x00fffff8;
    for (let i = 0; i < frames; i++) {
      const w = this.ramWords[(base + i * 4) >>> 2];
      samples[i * 2] = w >> 16;
      samples[i * 2 + 1] = (w << 16) >> 16;
    }
    this.onAudio(samples, rate);
  }

  /* --------------------------------------------------------------- RDP */
  rdpRun() {
    let at = this.dp.start, end = this.dp.end;
    while (at + 8 <= end) {
      const hi = this.ramWords[at >>> 2], lo = this.ramWords[(at + 4) >>> 2];
      at += 8;
      const cmd = hi >>> 24;
      if (cmd === 0xff) {                       // set colour image
        this.dp.img = lo & 0x00ffffff;
        this.dp.width = (hi & 0x3ff) + 1;
      } else if (cmd === 0xf7) {                // set fill colour
        this.dp.colour = lo & 0xffff;
      } else if (cmd === 0xf6) {                // fill rectangle
        this.rdpFill((lo >>> 14) & 0x3ff, (lo >>> 2) & 0x3ff,
                     (hi >>> 14) & 0x3ff, (hi >>> 2) & 0x3ff, this.dp.colour);
      } else if (cmd === 0xe9 || cmd === 0xe7 || cmd === 0xe8 ||
                 cmd === 0xe6 || cmd === 0xed || cmd === 0xef) {
        /* syncs, scissor, other modes */
      } else {
        throw new Error(`RDP command ${cmd.toString(16)}`);
      }
    }
  }

  rdpFill(x0, y0, x1, y1, colour) {
    const base = this.dp.img, width = this.dp.width;
    const pair = (((colour << 16) | colour) >>> 0);
    for (let y = y0; y <= y1; y++) {
      const row = base + y * width * 2;
      let x = x0;
      if (x & 1) { this.write16(row + x * 2, colour); x++; }
      for (; x + 1 <= x1; x += 2) this.ramWords[(row + x * 2) >>> 2] = pair;
      if (x <= x1) this.write16(row + x * 2, colour);
    }
  }

  /* ------------------------------------------------------------ joybus */
  joybus() {
    const b = this.pif;
    let i = 0, channel = 0;
    while (i < 64 && channel < 4) {
      const cmd = b[i];
      if (cmd === 0xfe) break;
      if (cmd === 0xff) { i++; continue; }
      if (cmd === 0x00 || cmd === 0xfd) { channel++; i++; continue; }
      const tx = cmd & 0x3f, rx = b[i + 1] & 0x3f;
      const op = tx ? b[i + 2] : 0, resp = i + 2 + tx;
      if (channel === 0) {                       // controller
        if (op === 0x00 || op === 0xff) {
          b[resp] = 0x05; b[resp + 1] = 0x00; b[resp + 2] = this.pak ? 0x01 : 0x02;
        } else if (op === 0x01) {
          b[resp] = (this.pad.buttons >> 8) & 255; b[resp + 1] = this.pad.buttons & 255;
          b[resp + 2] = this.pad.x & 255; b[resp + 3] = this.pad.y & 255;
        } else if ((op === 0x02 || op === 0x03) && this.pak) {
          this.pakTransfer(op, i, resp);
        } else b[i + 1] |= 0x40;
      } else if (channel === 1 && !this.mouseConnected) {
        b[i + 1] |= 0x80;                        // unplugged
      } else if (channel === 1) {                // mouse
        if (op === 0x00 || op === 0xff) { b[resp] = 0x02; b[resp + 1] = 0x00; b[resp + 2] = 0x00; }
        else if (op === 0x01) {
          b[resp] = (this.mouse.buttons >> 8) & 255; b[resp + 1] = this.mouse.buttons & 255;
          b[resp + 2] = this.mouse.dx & 255; b[resp + 3] = this.mouse.dy & 255;
          this.mouse.dx = 0; this.mouse.dy = 0;   // relative, and consumed
        } else b[i + 1] |= 0x40;
      } else if (channel === 2) {                // keyboard
        if (op === 0x00 || op === 0xff) { b[resp] = 0x00; b[resp + 1] = 0x02; b[resp + 2] = 0x00; }
        else if (op === 0x13) {
          let codes = this.keys;
          if (this.keyQueue.length) {
            if (this.keyPhase === 0) { codes = [this.keyQueue[0]]; this.keyPhase = 1; }
            else { codes = []; this.keyQueue.shift(); this.keyPhase = 0; }
          }
          for (let k = 0; k < 3; k++) {
            const code = codes[k] || 0;
            b[resp + k * 2] = (code >> 8) & 255; b[resp + k * 2 + 1] = code & 255;
          }
          b[resp + 6] = 0;
        } else b[i + 1] |= 0x40;
      } else {
        b[i + 1] |= 0x80;                        // nothing on this channel
      }
      i = resp + rx;
      channel++;
    }
  }

  /* ------------------------------------------------ the Controller Pak
   *
   * 32 bytes at a time, with the address's own five-bit CRC checked and
   * the data CRC computed the way the controller does -- so a kernel that
   * gets either wrong sees a failed transfer here, as it would on hardware.
   */
  static addressCrc(addr) {
    const table = [0, 0, 0, 0, 0, 0x15, 0x1f, 0x0b, 0x16, 0x19, 0x07, 0x0e, 0x1c, 0x0d, 0x1a, 0x01];
    let crc = 0;
    for (let i = 15; i >= 5; i--) if ((addr >> i) & 1) crc ^= table[i];
    return crc & 0x1f;
  }

  static dataCrc(bytes) {
    let crc = 0;
    for (let i = 0; i <= 32; i++) {
      for (let j = 7; j >= 0; j--) {
        const top = crc & 0x80;
        crc = (crc << 1) & 0xff;
        if (i < 32 && ((bytes[i] >> j) & 1)) crc |= 1;
        if (top) crc ^= 0x85;
      }
    }
    return crc;
  }

  pakTransfer(op, i, resp) {
    const b = this.pif;
    const word = (b[i + 3] << 8) | b[i + 4];
    const addr = word & 0xffe0;
    const good = N64.addressCrc(addr) === (word & 0x1f);
    if (op === 0x02) {
      const data = new Uint8Array(32);
      if (addr < 0x8000) data.set(this.pak.subarray(addr, addr + 32));
      b.set(data, resp);
      const crc = N64.dataCrc(data);
      b[resp + 32] = good ? crc : crc ^ 0xff;
    } else {
      const data = b.slice(i + 5, i + 5 + 32);
      if (good && addr < 0x8000) {
        this.pak.set(data, addr);
        if (this.onPakWrite) this.onPakWrite();
      }
      const crc = N64.dataCrc(data);
      b[resp] = good ? crc : crc ^ 0xff;
    }
  }

  /* ----------------------------------------------------------- execute */
  run(budget) {
    const end = this.icount + budget;
    while (this.icount < end) {
      const pc = this.pc;
      const instr = this.read32(pc);
      this.pc = (pc + 4) | 0;
      this.step(instr, pc);
      this.reg[0] = 0;
      this.icount++;
    }
  }

  branch(target, pc) {
    const delay = this.read32((pc + 4) >>> 0);
    this.pc = (pc + 8) | 0;
    this.step(delay, (pc + 4) >>> 0);
    this.reg[0] = 0;
    this.icount++;
    this.pc = target | 0;
  }

  step(instr, pc) {
    const reg = this.reg;
    const op = instr >>> 26;
    const rs = (instr >>> 21) & 31, rt = (instr >>> 16) & 31;
    const rd = (instr >>> 11) & 31, sa = (instr >>> 6) & 31;
    const imm = instr & 0xffff;
    const simm = (imm & 0x8000) ? imm - 0x10000 : imm;

    switch (op) {
      case 0: {
        const fn = instr & 0x3f;
        switch (fn) {
          case 0:  reg[rd] = reg[rt] << sa; return;
          case 2:  reg[rd] = reg[rt] >>> sa; return;
          case 3:  reg[rd] = reg[rt] >> sa; return;
          case 4:  reg[rd] = reg[rt] << (reg[rs] & 31); return;
          case 6:  reg[rd] = reg[rt] >>> (reg[rs] & 31); return;
          case 7:  reg[rd] = reg[rt] >> (reg[rs] & 31); return;
          case 8:  this.branch(reg[rs], pc); return;
          case 9: { const t = reg[rs]; reg[rd || 31] = (pc + 8) | 0; this.branch(t, pc); return; }
          case 10: if (reg[rt] === 0) reg[rd] = reg[rs]; return;
          case 11: if (reg[rt] !== 0) reg[rd] = reg[rs]; return;
          case 13: throw new Error(`break at ${(pc >>> 0).toString(16)}`);
          case 15: return;                                   // SYNC
          case 16: reg[rd] = this.hi; return;
          case 17: this.hi = reg[rs]; return;
          case 18: reg[rd] = this.lo; return;
          case 19: this.lo = reg[rs]; return;
          case 24: case 25: {
            const a = fn === 24 ? BigInt(reg[rs]) : BigInt(reg[rs] >>> 0);
            const b = fn === 24 ? BigInt(reg[rt]) : BigInt(reg[rt] >>> 0);
            const p = BigInt.asUintN(64, a * b);
            this.lo = Number(BigInt.asIntN(32, p & 0xffffffffn));
            this.hi = Number(BigInt.asIntN(32, p >> 32n));
            return;
          }
          case 26: {
            const a = reg[rs], b = reg[rt];
            if (b === 0) { this.lo = 0; this.hi = a; }
            else { this.lo = (a / b) | 0; this.hi = (a % b) | 0; }
            return;
          }
          case 27: {
            const a = reg[rs] >>> 0, b = reg[rt] >>> 0;
            if (b === 0) { this.lo = -1; this.hi = a | 0; }
            else { this.lo = ((a / b) >>> 0) | 0; this.hi = (a % b) | 0; }
            return;
          }
          case 32: case 33: reg[rd] = (reg[rs] + reg[rt]) | 0; return;
          case 34: case 35: reg[rd] = (reg[rs] - reg[rt]) | 0; return;
          case 36: reg[rd] = reg[rs] & reg[rt]; return;
          case 37: reg[rd] = reg[rs] | reg[rt]; return;
          case 38: reg[rd] = reg[rs] ^ reg[rt]; return;
          case 39: reg[rd] = ~(reg[rs] | reg[rt]); return;
          case 42: reg[rd] = reg[rs] < reg[rt] ? 1 : 0; return;
          case 43: reg[rd] = (reg[rs] >>> 0) < (reg[rt] >>> 0) ? 1 : 0; return;
          case 52: if (reg[rs] === reg[rt]) throw new Error("trap"); return;
          default: throw new Error(`SPECIAL ${fn} at ${(pc >>> 0).toString(16)}`);
        }
      }
      case 1: {
        if (rt === 0 || rt === 16) {
          if (rt === 16) reg[31] = (pc + 8) | 0;
          if (reg[rs] < 0) this.branch(pc + 4 + (simm << 2), pc);
        } else if (rt === 1 || rt === 17) {
          if (rt === 17) reg[31] = (pc + 8) | 0;
          if (reg[rs] >= 0) this.branch(pc + 4 + (simm << 2), pc);
        } else throw new Error(`REGIMM ${rt}`);
        return;
      }
      case 2: this.branch((((pc + 4) & 0xf0000000) | ((instr & 0x3ffffff) << 2)) | 0, pc); return;
      case 3: reg[31] = (pc + 8) | 0;
        this.branch((((pc + 4) & 0xf0000000) | ((instr & 0x3ffffff) << 2)) | 0, pc); return;
      case 4: if (reg[rs] === reg[rt]) this.branch(pc + 4 + (simm << 2), pc); return;
      case 5: if (reg[rs] !== reg[rt]) this.branch(pc + 4 + (simm << 2), pc); return;
      case 6: if (reg[rs] <= 0) this.branch(pc + 4 + (simm << 2), pc); return;
      case 7: if (reg[rs] > 0) this.branch(pc + 4 + (simm << 2), pc); return;
      case 8: case 9: reg[rt] = (reg[rs] + simm) | 0; return;
      case 10: reg[rt] = reg[rs] < simm ? 1 : 0; return;
      case 11: reg[rt] = (reg[rs] >>> 0) < (simm >>> 0) ? 1 : 0; return;
      case 12: reg[rt] = reg[rs] & imm; return;
      case 13: reg[rt] = reg[rs] | imm; return;
      case 14: reg[rt] = reg[rs] ^ imm; return;
      case 15: reg[rt] = imm << 16; return;
      case 16:
        if (rs === 0) reg[rt] = this.cp0[rd];
        else if (rs === 4) this.cp0[rd] = reg[rt];
        else throw new Error("COP0");
        return;
      case 32: { const v = this.read8(reg[rs] + simm); reg[rt] = (v & 0x80) ? v - 256 : v; return; }
      case 33: { const v = this.read16(reg[rs] + simm); reg[rt] = (v & 0x8000) ? v - 65536 : v; return; }
      case 35: reg[rt] = this.read32(reg[rs] + simm) | 0; return;
      case 36: reg[rt] = this.read8(reg[rs] + simm); return;
      case 37: reg[rt] = this.read16(reg[rs] + simm); return;
      case 40: this.write8(reg[rs] + simm, reg[rt]); return;
      case 41: this.write16(reg[rs] + simm, reg[rt]); return;
      case 43: this.write32(reg[rs] + simm, reg[rt]); return;
      case 47: return;                                        // CACHE
      case 34: case 38: {                                     // LWL / LWR
        const addr = (reg[rs] + simm) >>> 0;
        const aligned = this.read32(addr & ~3);
        let shift = (addr & 3) * 8;
        if (op === 34) reg[rt] = ((aligned << shift) | (reg[rt] & ((1 << shift) - 1))) | 0;
        else {
          shift = 24 - shift;
          const mask = shift ? (-1 << (32 - shift)) : 0;
          reg[rt] = ((aligned >>> shift) | (reg[rt] & mask)) | 0;
        }
        return;
      }
      case 42: case 46: {                                     // SWL / SWR
        const addr = (reg[rs] + simm) >>> 0;
        const aligned = this.read32(addr & ~3);
        let shift = (addr & 3) * 8;
        if (op === 42) {
          const mask = shift ? (-1 << (32 - shift)) : 0;
          this.write32(addr & ~3, (aligned & mask) | ((reg[rt] >>> 0) >>> shift));
        } else {
          shift = 24 - shift;
          const mask = shift ? ((1 << shift) - 1) : 0;
          this.write32(addr & ~3, (aligned & mask) | ((reg[rt] << shift) >>> 0));
        }
        return;
      }
      default: throw new Error(`opcode ${op} at ${(pc >>> 0).toString(16)}`);
    }
  }

  /* ------------------------------------------------------------- video */
  paint(imageData) {
    const ctrl = this.vi[0];
    if ((ctrl & 3) !== 2) return false;
    const origin = this.vi[1] & 0x00ffffff;
    const width = this.vi[2] || 640;
    const height = 480;
    const out = imageData.data;
    const ram = this.ramWords;
    let o = 0;
    for (let y = 0; y < height; y++) {
      let addr = origin + y * width * 2;
      for (let x = 0; x < width; x += 2, addr += 4) {
        const w = ram[addr >>> 2];
        for (let half = 0; half < 2; half++) {
          const p = half ? (w & 0xffff) : (w >>> 16);
          const r = (p >>> 11) & 31, g = (p >>> 6) & 31, b = (p >>> 1) & 31;
          out[o++] = (r << 3) | (r >> 2);
          out[o++] = (g << 3) | (g >> 2);
          out[o++] = (b << 3) | (b >> 2);
          out[o++] = 255;
        }
      }
    }
    return true;
  }
}
