/* pakfs.js -- the Controller Pak's file system, from the page's side, so
 * files can be carried between the pak and the computer.
 *
 * The format is src/fs.c's: page 0 holds the header and a byte-a-page
 * allocation table, pages 1-3 the directory, pages 4-127 the data.  Every
 * change bumps the header's generation, which is how the running system
 * knows to read the directory again before it trusts its own copy.
 */
const PAGE = 256, PAGES = 128, META = 4, FAT = 128, DIR = 256, ENTRY = 32;
const NAME_MAX = 19, DIR_MAX = 24, FREE = 0x00, END = 0xff, SYSTEM = 0xfe;
const MAGIC = [0x4e, 0x34, 0x54, 0x48, 0x46, 0x53, 0, 1];        // N4THFS 0 1

const get16 = (p, o) => (p[o] << 8) | p[o + 1];
const put16 = (p, o, v) => { p[o] = (v >> 8) & 255; p[o + 1] = v & 255; };

function checksum(p) {
  let sum = 0x4e34;
  for (let i = 16; i < META * PAGE; i++) sum = (sum * 31 + p[i]) & 0xffff;
  return sum;
}

function commit(p) {
  put16(p, 10, (get16(p, 10) + 1) & 0xffff);
  put16(p, 8, checksum(p));
}

/* 'ok', 'blank' (nothing on it: safe to format), 'foreign' or 'damaged'. */
export function state(p) {
  if (MAGIC.every((b, i) => p[i] === b)) return get16(p, 8) === checksum(p) ? 'ok' : 'damaged';
  return p.subarray(0, META * PAGE).every((b) => b === 0) ? 'blank' : 'foreign';
}

export function format(p) {
  p.fill(0);
  p.set(MAGIC, 0);
  p.set([...'CONTROLLER PAK'].map((c) => c.charCodeAt(0)), 16);
  for (let i = 0; i < META; i++) p[FAT + i] = SYSTEM;
  commit(p);
}

export function list(p) {
  const out = [];
  for (let i = 0; i < DIR_MAX; i++) {
    const e = DIR + i * ENTRY;
    if (!p[e]) continue;
    let name = '';
    for (let k = 0; k < NAME_MAX && p[e + k]; k++) name += String.fromCharCode(p[e + k]);
    out.push({ slot: i, name, size: get16(p, e + 20), first: p[e + 22] });
  }
  return out.sort((a, b) => (a.name.toUpperCase() < b.name.toUpperCase() ? -1 : 1));
}

export function free(p) {
  let n = 0;
  for (let i = META; i < PAGES; i++) if (p[FAT + i] === FREE) n++;
  return n * PAGE;
}

export function read(p, file) {
  const out = new Uint8Array(file.size);
  let page = file.first, at = 0;
  while (at < file.size && page >= META && page < PAGES) {
    const n = Math.min(PAGE, file.size - at);
    out.set(p.subarray(page * PAGE, page * PAGE + n), at);
    at += n;
    page = p[FAT + page];
  }
  return out;
}

/* A host file name made into one the system can hold. */
export function saneName(name) {
  let n = name.replace(/\s+/g, '_').replace(/[^!-~]/g, '').replace(/:/g, '_');
  if (n.length > NAME_MAX) {
    const dot = n.lastIndexOf('.');
    const ext = dot > 0 && n.length - dot <= 5 ? n.slice(dot) : '';
    n = n.slice(0, NAME_MAX - ext.length) + ext;
  }
  return n || 'FILE';
}

function freeChain(p, page) {
  for (let guard = 0; page >= META && page < PAGES && guard < PAGES; guard++) {
    const next = p[FAT + page];
    p[FAT + page] = FREE;
    page = next;
  }
}

export function remove(p, name) {
  const f = list(p).find((x) => x.name.toUpperCase() === name.toUpperCase());
  if (!f) return false;
  freeChain(p, f.first);
  p.fill(0, DIR + f.slot * ENTRY, DIR + (f.slot + 1) * ENTRY);
  commit(p);
  return true;
}

/* Write a whole file, replacing one of the same name.  Throws with a
 * sentence if it cannot. */
export function write(p, name, bytes) {
  if (state(p) === 'blank') format(p);
  if (state(p) !== 'ok') throw new Error('this pak is not formatted for n64forthos: FORMAT it on the machine first');
  name = saneName(name);
  const old = list(p).find((x) => x.name.toUpperCase() === name.toUpperCase());
  const need = Math.ceil(bytes.length / PAGE);
  if (bytes.length > (PAGES - META) * PAGE) throw new Error(`${name} is too big for a pak`);
  if (need * PAGE > free(p) + (old ? Math.ceil(old.size / PAGE) * PAGE : 0))
    throw new Error(`no room on the pak for ${name}`);
  let slot = old ? old.slot : -1;
  if (slot < 0) for (let i = 0; i < DIR_MAX && slot < 0; i++) if (!p[DIR + i * ENTRY]) slot = i;
  if (slot < 0) throw new Error('the pak\'s directory is full (24 files)');
  if (old) freeChain(p, old.first);
  let first = END, prev = -1, at = 0;
  for (let page = META; page < PAGES && at < bytes.length; page++) {
    if (p[FAT + page] !== FREE) continue;
    p.fill(0, page * PAGE, (page + 1) * PAGE);
    p.set(bytes.subarray(at, at + PAGE), page * PAGE);
    at += PAGE;
    p[FAT + page] = END;
    if (prev < 0) first = page; else p[FAT + prev] = page;
    prev = page;
  }
  const e = DIR + slot * ENTRY;
  p.fill(0, e, e + ENTRY);
  for (let k = 0; k < name.length; k++) p[e + k] = name.charCodeAt(k);
  put16(p, e + 20, bytes.length);
  p[e + 22] = first;
  commit(p);
  return name;
}
