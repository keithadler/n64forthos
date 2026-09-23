// web.mjs -- drive web/n64emu.js from Node, for tests that would take the
// Python emulator minutes: boot the cartridge, press buttons, type, and read
// the screen back as text by matching every 8x16 cell against the font the
// kernel draws with (the same trick as tools/screen.py).
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { N64 } from '../web/n64emu.js';

const HERE = dirname(fileURLToPath(import.meta.url));
export const PER_FRAME = 1_562_500;           // 93.75 MHz / 60
export const PAD = { A: 0x8000, B: 0x4000, Z: 0x2000, START: 0x1000,
                     UP: 0x0800, DOWN: 0x0400, LEFT: 0x0200, RIGHT: 0x0100,
                     L: 0x0020, R: 0x0010, CUP: 0x0008, CDOWN: 0x0004,
                     CLEFT: 0x0002, CRIGHT: 0x0001 };
export const KEY = { ESC: 0x1b, UP: 0x80, DOWN: 0x81, LEFT: 0x82, RIGHT: 0x83,
                     HOME: 0x84, END: 0x85, PGUP: 0x86, PGDN: 0x87, DEL: 0x88,
                     ctrl: (c) => c.toUpperCase().charCodeAt(0) & 0x1f };

function loadFont() {
  const src = readFileSync(join(HERE, '..', 'src', 'font.h'), 'utf8');
  const rows = [...src.matchAll(/\{((?:0x[0-9a-f]{2},){15}0x[0-9a-f]{2})\}/g)];
  const glyphs = new Map(), trimmed = new Map();
  rows.forEach((m, i) => {
    const bits = m[1].split(',').map((b) => parseInt(b, 16));
    const ch = String.fromCharCode(0x20 + i);
    if (!glyphs.has(bits.join())) glyphs.set(bits.join(), ch);
    if (!trimmed.has(bits.slice(1).join())) trimmed.set(bits.slice(1).join(), ch);
  });
  return { glyphs, trimmed };
}
const FONT = loadFont();

export class Machine {
  constructor(romPath = join(HERE, '..', 'build', 'n64forthos.z64'), opts = {}) {
    const rom = readFileSync(romPath).subarray(0, 2 << 20);
    this.emu = new N64(new Uint8Array(rom));
    if (opts.pak === null) this.emu.pak = null;
    else if (opts.pak) this.emu.pak = opts.pak;
    this.emu.boot();
  }

  frames(n = 1) {
    for (let i = 0; i < n; i++) this.emu.run(PER_FRAME);
  }

  press(mask, hold = 4, gap = 6) {
    this.emu.pad.buttons |= mask;
    this.frames(hold);
    this.emu.pad.buttons &= ~mask;
    this.frames(gap);
  }

  /* Type on the emulated keyboard, the way the page does: into the queue
   * the emulator hands out a key a poll.  A number is a raw key code. */
  type(text, settle = 4) {
    const codes = typeof text === 'number' ? [text]
      : [...text].map((c) => (c === '\n' ? 13 : c.charCodeAt(0)));
    this.emu.keyQueue.push(...codes);
    for (let f = 0; f < 4000 && this.emu.keyQueue.length; f++) this.frames(1);
    this.frames(settle);
  }

  /* Wait until the screen shows some text, or give up. */
  waitFor(needle, maxFrames = 600, step = 10) {
    for (let f = 0; f < maxFrames; f += step) {
      if (this.text().includes(needle)) return true;
      this.frames(step);
    }
    return this.text().includes(needle);
  }

  pixel(x, y) {
    const origin = this.emu.vi[1] & 0x00ffffff;
    const addr = origin + (y * 640 + x) * 2;
    const w = this.emu.ramWords[addr >>> 2];
    return (addr & 2) ? (w & 0xffff) : (w >>> 16);
  }

  lines() {
    const out = [];
    for (let row = 0; row < 30; row++) {
      let line = '';
      for (let col = 0; col < 80; col++) {
        const cell = [], counts = new Map();
        for (let y = 0; y < 16; y++)
          for (let x = 0; x < 8; x++) {
            const p = this.pixel(col * 8 + x, row * 16 + y);
            cell.push(p);
            counts.set(p, (counts.get(p) || 0) + 1);
          }
        let bg = cell[0], most = 0;
        for (const [p, n] of counts) if (n > most) { most = n; bg = p; }
        const bits = [];
        for (let y = 0; y < 16; y++) {
          let b = 0;
          for (let x = 0; x < 8; x++) if (cell[y * 8 + x] !== bg) b |= 0x80 >> x;
          bits.push(b);
        }
        line += FONT.glyphs.get(bits.join()) || FONT.trimmed.get(bits.slice(1).join()) || '░';
      }
      out.push(line.trimEnd());
    }
    return out;
  }

  text() {
    return this.lines().join('\n');
  }

  /* The screen as a PNG, the 474 lines an NTSC set shows. */
  png(path, height = 474) {
    const crcTable = Array.from({ length: 256 }, (_, n) => {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      return c >>> 0;
    });
    const crc = (buf) => {
      let c = 0xffffffff;
      for (const b of buf) c = crcTable[(c ^ b) & 255] ^ (c >>> 8);
      return (c ^ 0xffffffff) >>> 0;
    };
    const chunk = (type, data) => {
      const out = Buffer.alloc(12 + data.length);
      out.writeUInt32BE(data.length, 0);
      out.write(type, 4, 'ascii');
      data.copy(out, 8);
      out.writeUInt32BE(crc(out.subarray(4, 8 + data.length)), 8 + data.length);
      return out;
    };
    const raw = Buffer.alloc(height * (1 + 640 * 3));
    for (let y = 0; y < height; y++) {
      const row = y * (1 + 640 * 3);
      for (let x = 0; x < 640; x++) {
        const p = this.pixel(x, y), o = row + 1 + x * 3;
        const r = (p >>> 11) & 31, g = (p >>> 6) & 31, b = (p >>> 1) & 31;
        raw[o] = (r << 3) | (r >> 2); raw[o + 1] = (g << 3) | (g >> 2); raw[o + 2] = (b << 3) | (b >> 2);
      }
    }
    const head = Buffer.alloc(13);
    head.writeUInt32BE(640, 0); head.writeUInt32BE(height, 4);
    head[8] = 8; head[9] = 2;
    writeFileSync(path, Buffer.concat([
      Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
      chunk('IHDR', head), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
    ]));
  }
}
