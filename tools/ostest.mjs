// ostest.mjs -- the operating system's own checks, on the browser emulator
// in Node: files, the pak, the editor, the shell, Files, BOOT.FTH.  Fast
// enough to run on every change (make ostest), where tools/check.py is the
// slower, older suite on the Python emulator.
import { Machine, PAD, KEY } from './web.mjs';
import * as pakfs from '../web/pakfs.js';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const failures = [];
function check(ok, what) {
  console.log(`  ${ok ? 'pass' : 'FAIL'}  ${what}`);
  if (!ok) failures.push(what);
}

/* The Forth suite, twice: with a Controller Pak, and with none (RAM disk). */
function forthSuite(pak) {
  const m = new Machine(join(HERE, '..', 'build', 'n64forthos-test.z64'),
                        { pak: pak ? new Uint8Array(32768) : null });
  for (let i = 0; i < 400; i++) {
    m.frames(5);
    const w = (a) => m.emu.ramWords[a >>> 2] >>> 0;
    if (w(0x380000) === 0x54535431) {
      const passes = w(0x380004), fails = w(0x380008);
      console.log(`        ${passes} assertions passed, ${fails} failed`);
      check(fails === 0, `tests.fth, ${pak ? 'on a Controller Pak' : 'on the RAM disk'}: no failures`);
      check(passes >= 170, `tests.fth ran its file tests too (${passes} assertions)`);
      return;
    }
  }
  check(false, 'the test cartridge reached REPORT');
}

function toConsole(m) {
  m.frames(90);
  m.press(PAD.DOWN); m.press(PAD.A); m.frames(20);
}

console.log('tests.fth');
forthSuite(true);
forthSuite(false);

console.log('a blank Controller Pak');
{
  const m = new Machine();
  toConsole(m);
  m.type('DIR\n'); m.frames(30);
  check(m.text().includes('31744 bytes free on the Controller Pak'),
        'a blank pak is formatted and mounted');
  check(m.text().includes('README.TXT') && m.text().includes('ROM'),
        'DIR lists the ROM files');
}

console.log('the editor, and a file that outlives the power');
{
  let m = new Machine();
  toConsole(m);
  m.type('EDIT SQUARES.FTH\n'); m.frames(20);
  check(m.text().includes('EDIT  SQUARES.FTH') && m.text().includes('a new file'),
        'EDIT opens the editor on a new file');
  m.type(': SQUARES 6 1 DO I DUP * . LOOP CR ;\nSQUARES\n');
  m.type(KEY.UP); m.type(KEY.HOME); m.type('\\ written on the N64\n');
  check(/2 \\ written on the N64/.test(m.text()), 'arrows and Home move the cursor');
  m.type(KEY.ctrl('r')); m.frames(60);
  check(m.text().includes('1 4 9 16 25'), '^R saves and runs it');
  m.type('CAT SQUARES.FTH\n'); m.frames(30);
  check(m.text().includes('\\ written on the N64'), 'CAT shows what was saved');
  m.type('EDIT SQUARES.FTH\n'); m.frames(20);
  m.type(KEY.DOWN); m.type(KEY.ctrl('k')); m.type('x');
  m.type(KEY.ESC); m.frames(10);
  check(m.text().includes('not saved'), 'leaving with changes asks first');
  m.type(KEY.ESC); m.frames(20);
  m.type('CAT SQUARES.FTH\n'); m.frames(30);
  check(m.text().includes('written on the N64'), 'and throwing them away keeps the file');
  m.type('EDIT LINES.TXT\n'); m.frames(20);
  m.type('one\ntwo\nthree\n');
  m.type(KEY.UP); m.type(KEY.UP); m.type(KEY.UP); m.type(KEY.UP);
  m.type(KEY.ctrl('k')); m.type(KEY.ctrl('k'));   // cut "one" and "two"
  m.type(KEY.DOWN); m.type(KEY.ctrl('u'));         // under "three"
  check(/1 three\n +2 one\n +3 two/.test(m.text()), '^K twice and ^U move two lines');
  m.type(KEY.ctrl('f')); m.type('TWO\n');
  check(m.text().includes('found') && /line 3 col 1/.test(m.text()),
        '^F finds, ignoring case, and puts the cursor there');
  m.type(KEY.ctrl('f')); m.type('nowhere\n');
  check(m.text().includes('not found'), 'and says when it cannot');
  m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(10);
  m.type('EDIT BOOT.FTH\n'); m.frames(20);
  m.type(': HI ." hello from BOOT.FTH" CR ; HI\n'); m.type(KEY.ctrl('s')); m.frames(10);
  check(m.text().includes('saved to the Controller Pak'), '^S saves');
  m.type(KEY.ESC); m.frames(20);

  const pak = m.emu.pak;
  m = new Machine(undefined, { pak });
  let boot = '';
  for (let i = 0; i < 60 && !boot; i++) {
    m.frames(2);
    if (m.text().includes('hello from BOOT.FTH')) boot = m.text();
  }
  check(boot.includes('BOOT   BOOT.FTH'), 'BOOT.FTH runs at power on');
  m.frames(60);
  check(m.text().includes('Controller Pak: 3 files'), 'the desktop counts the files on the pak');
  m.press(PAD.A); m.frames(30);                         // Files
  const files = m.text();
  check(files.includes('> BOOT.FTH') && files.includes('SQUARES.FTH') &&
        files.includes('LINES.TXT') &&
        files.includes('WM.FTH'), 'Files lists the pak and the ROM');
  m.press(PAD.DOWN); m.press(PAD.DOWN);
  m.press(PAD.START); m.frames(60);                    // run SQUARES.FTH
  check(m.text().includes('INCLUDE SQUARES.FTH') && m.text().includes('1 4 9 16 25'),
        'START in Files runs a program at the prompt');
  m.press(PAD.L); m.frames(30);                         // back to Files
  check(m.text().includes('> SQUARES.FTH'), 'the prompt goes back to Files');
  m.press(PAD.Z); m.frames(5);
  check(m.text().includes('again to delete'), 'delete asks for a second press');
  m.press(PAD.Z); m.frames(20);
  check(!m.text().includes('SQUARES.FTH') && m.text().includes('deleted'),
        'and then deletes');
}

console.log('the shell');
{
  const m = new Machine();
  toConsole(m);
  m.type('COPY HELLO.FTH MINE.FTH\n'); m.frames(30);
  m.type('REN MINE.FTH OURS.FTH\n'); m.frames(30);
  m.type('DIR\n'); m.frames(30);
  const t = m.text();
  check(t.includes('copied') && t.includes('renamed'), 'COPY and REN report');
  check(/OURS\.FTH +600 PAK/.test(t), 'the copy is on the pak, the size of the original');
  m.type('EDIT SCRATCH.FTH\n'); m.frames(10); m.type(KEY.ESC); m.frames(10);
  check(m.text().includes('copied') && m.text().includes('renamed'),
        'the console keeps its text across the editor');
  m.type('.( said now) : Q .( and while compiling) ;\n'); m.frames(10);
  check(m.text().includes('said nowand while compiling'), '.( speaks at once');
  m.type('INCLUDE OURS.FTH\n'); m.frames(60);
  check(m.text().includes('liftoff'), 'INCLUDE runs it');
  m.type('DEL OURS.FTH\n'); m.frames(30);
  m.type('CAT OURS.FTH\n'); m.frames(30);
  check(m.text().includes('no such file'), 'DEL removes it');
  m.type('DEL MANDEL.FTH\n'); m.frames(30);
  check(m.text().includes('in ROM and cannot be changed'), 'ROM files cannot be deleted');
  m.type('FORMAT\n'); m.frames(30);
  check(m.text().includes('ERASE-PAK'), 'FORMAT warns before it erases');
  m.type(KEY.UP); m.type(KEY.UP); m.frames(4);
  const prompt = () => m.lines().filter((l) => l.trim().startsWith('ok>')).pop().replace(/\u2591/g, '').trim();
  check(prompt() === 'ok> DEL MANDEL.FTH', `up recalls earlier lines (${prompt()})`);
  m.type(KEY.DOWN); m.type(KEY.DOWN);
  check(prompt() === 'ok>', 'and down comes back to an empty line');
}

console.log('a program that asks');
{
  const m = new Machine();
  toConsole(m);
  m.type('CREATE BUF 40 ALLOT\n');
  m.type(': ASK ." your name? " BUF 40 ACCEPT CR ." hello, " BUF SWAP TYPE CR ;\n');
  m.type('ASK\n'); m.frames(10);
  check(m.text().includes('your name?'), 'ACCEPT waits at the prompt it was given');
  m.type('Ada\n'); m.frames(10);
  check(m.text().includes('hello, Ada'), 'and hands back what was typed');
}

console.log('the break key');
{
  const m = new Machine();
  toConsole(m);
  const stops = (label, setup, press) => {
    m.type('PAGE ' + setup + '\n', 0); m.frames(30);
    press(); m.frames(40);
    m.type('7 6 * .\n'); m.frames(20);
    const t = m.lines().join('\n');
    check(t.includes('interrupted') && /\n\s*42\s*\n/.test(t), label);
  };
  stops('Esc stops a compiled BEGIN AGAIN', ': SPIN BEGIN AGAIN ; SPIN', () => m.type(KEY.ESC, 0));
  stops('^C stops a compiled DO LOOP, stack intact',
        ': LONG 2000000000 0 DO LOOP ; LONG', () => m.type(KEY.ctrl('c'), 0));
  stops('START and Z stop a loop that calls things',
        ': BUSY BEGIN FRAMES DROP 0 UNTIL ; BUSY',
        () => { m.emu.pad.buttons = PAD.START | PAD.Z; m.frames(30); m.emu.pad.buttons = 0; });
  m.type('EDIT LOOPS.FTH\n'); m.frames(10);
  m.type('.( one) CR\n: FOREVER BEGIN AGAIN ; FOREVER\n.( never) CR\n');
  m.type(KEY.ctrl('r'), 0); m.frames(60); m.type(KEY.ESC, 0); m.frames(40);
  check(m.text().includes('one') && m.text().includes('interrupted') && !m.text().includes('never'),
        'and a break stops the rest of the file too');
}

console.log('three programs at once');
{
  const m = new Machine();
  m.emu.mouseConnected = false;
  m.frames(90);
  for (let i = 0; i < 7; i++) m.press(PAD.DOWN);
  m.press(PAD.A); m.frames(300);
  const t = m.text();
  check(t.includes('MANDEL.FTH') && t.includes('LIFE.FTH') && t.includes('NAVIER.FTH'),
        'Tasks opens a window for each of three apps');
  const grab = (x0) => { const a = []; for (let y = 72; y < 256; y += 3) for (let x = x0; x < x0 + 184; x += 3) a.push(m.pixel(x, y)); return a; };
  const lifeA = grab(230), navA = grab(438), manA = grab(22);
  m.frames(300);
  const lifeB = grab(230), navB = grab(438);
  const changed = (a, b) => a.filter((p, i) => p !== b[i]).length;
  check(new Set(manA).size > 6, `Mandelbrot has drawn its picture (${new Set(manA).size} colours)`);
  check(changed(lifeA, lifeB) > 50 && changed(navA, navB) > 50,
        `and Life and Navier-Stokes are both still moving (${changed(lifeA, lifeB)}, ${changed(navA, navB)})`);
}

console.log('a prompt in a window, beside running programs');
{
  const m = new Machine();
  m.emu.mouseConnected = false;
  m.frames(90);
  for (let i = 0; i < 7; i++) m.press(PAD.DOWN);
  m.press(PAD.A); m.frames(200);
  const prompt = () => m.lines().slice(18, 28).map((l) => l.replace(/\u2591/g, ' ')).join('\n');
  const life = () => { const a = []; for (let y = 72; y < 256; y += 3) for (let x = 230; x < 414; x += 3) a.push(m.pixel(x, y)); return a; };
  m.type('PAGE 2 3 + .\n'); m.frames(5);
  check(/\n\s*5\s*\n/.test(prompt()), 'a line typed in the window runs there');
  m.type('1 2 3\n'); m.type('+ + .\n'); m.frames(5);
  check(/\n\s*6\s*\n/.test(prompt()), 'and the stack carries from one line to the next');
  const before = life();
  m.type('PAGE NOPE\n'); m.frames(5);
  check(prompt().includes('undefined: NOPE'), 'an error is reported in the window');
  m.type('7 6 * .\n'); m.frames(60);
  check(/\n\s*42\s*\n/.test(prompt()), 'and the desk and its prompt carry on');
  check(life().filter((p, i) => p !== before[i]).length > 50, 'Life kept going while we typed');
  m.type(KEY.ESC); m.frames(20);
  check(m.text().includes('LIFE.FTH'), 'Esc typed at the prompt does not leave the desk');
  m.type('PAGE : SPIN BEGIN AGAIN ; SPIN\n', 0); m.frames(30);
  m.type(KEY.ctrl('c'), 0); m.frames(40);
  m.type('9 9 * .\n'); m.frames(10);
  check(prompt().includes('interrupted') && /\n\s*81\s*\n/.test(prompt()) &&
        m.text().includes('LIFE.FTH'), '^C stops a command at the prompt, not the desk');
  m.type('EDIT DESKNOTE.TXT\n'); m.frames(20);
  check(m.lines()[3].includes('DESKNOTE.TXT') && m.text().includes('a new file'),
        'EDIT from the window prompt opens the editor in a window of its own');
  m.type('written from the desk\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(40);
  check(m.text().includes('MANDEL.FTH') && m.text().includes('prompt'),
        'and leaving it puts the desk back');
  m.type('BYE\n'); m.frames(30);
  check(m.text().includes('Pick something'), 'BYE goes back to the desktop');
}

console.log('the desk: the window manager as a shell');
{
  const m = new Machine();
  m.emu.mouseConnected = false;
  m.frames(90);
  for (let i = 0; i < 8; i++) m.press(PAD.DOWN);
  m.press(PAD.A); m.frames(60);
  const prompt = () => m.lines().slice(18, 28).map((l) => l.replace(/\u2591/g, ' ')).join('\n');
  check(m.text().includes('prompt') && !m.text().includes('LIFE.FTH'), 'the desk opens with a prompt');
  m.type('OPEN LIFE.FTH\n'); m.frames(60);
  check(m.text().includes('LIFE.FTH'), 'OPEN puts an app in a window of its own');
  m.type('PAGE OPEN HELLO.FTH\n'); m.frames(30);
  check(prompt().includes('Hello from a file') && !m.text().includes(' HELLO.FTH  '),
        'a file that is not an app just runs, at the prompt');
  m.type('PAGE OPEN NOSUCH.FTH\n'); m.frames(20);
  m.type('6 7 * .\n'); m.frames(20);
  check(prompt().includes('no such file') && /\n\s*42\s*\n/.test(prompt()),
        'a missing file is an error, and the desk carries on');
  m.type('OPEN MANDEL.FTH\n'); m.frames(60);
  m.type('PAGE DEPTH . 2 3 + .\n'); m.frames(20);
  check(/\n\s*0 5\s*\n/.test(prompt()),
        "the apps' own words (Mandelbrot's DEPTH) stay out of the prompt's way");
  m.type('FILES\n'); m.frames(30);
  check(m.text().includes('Controller Pak:') && m.text().includes('bytes free'),
        'FILES brings up the Files window');
  m.press(PAD.B); m.frames(30);
  check(m.text().includes('LIFE.FTH') && m.text().includes('MANDEL.FTH') && m.text().includes('prompt'),
        'and B comes back to the desk as it was');
  m.type('EDIT CUBE.FTH\n'); m.frames(20);
  check(m.text().includes('CUBE.FTH') && m.text().includes('a new file') &&
        m.text().includes('prompt'), 'EDIT on the desk opens the editor in a window');
  m.type(': CUBE DUP DUP * * ;\n.( cubed: ) 3 CUBE . CR\n');
  m.type(KEY.ctrl('r')); m.frames(30);
  check(prompt().includes('cubed: 27'), '^R saves it and runs it at the prompt');
  m.type(KEY.ctrl('o')); m.frames(10);
  m.type('5 CUBE .\n'); m.frames(10);
  check(/\n\s*125\s*\n/.test(prompt()), '^O gives the prompt the keys, and CUBE is there to use');
  m.type(KEY.ctrl('o')); m.frames(10);
  m.type('\\ a comment\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(20);
  check(!m.lines()[3].includes('CUBE.FTH') && m.text().includes('prompt'),
        'and back to the editor, which saves and closes');
  m.type('CAT CUBE.FTH\n'); m.frames(20);
  check(prompt().includes('a comment'), 'with everything typed in it on the pak');
  m.type('BYE\n'); m.frames(30);
  check(m.text().includes('Pick something'), 'BYE leaves the desk');
}

console.log('the desk as the shell, from BOOT.FTH');
{
  let m = new Machine();
  toConsole(m);
  m.type('EDIT BOOT.FTH\n'); m.frames(10);
  m.type('RUN DESK.FTH\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(10);
  const pak = m.emu.pak;
  m = new Machine(undefined, { pak });
  m.emu.mouseConnected = false;
  m.frames(150);
  check(m.text().includes('prompt') && m.text().includes('red box closes') &&
        !m.text().includes('Pick something'), 'with RUN DESK.FTH in BOOT.FTH, the machine starts on the desk');
  m.type('OPEN LIFE.FTH\n'); m.frames(40);
  check(m.text().includes('LIFE.FTH'), 'and it works as one');
  m.type('BYE\n'); m.frames(30);
  check(m.text().includes('Pick something'), 'BYE from it reaches the desktop');
}

console.log('an animation that runs for ever');
{
  const m = new Machine();
  toConsole(m);
  m.type('EDIT SPIN.FTH\n'); m.frames(10);
  m.type('1 CONSTANT ROWS\n: ROW DROP ;\n: NEXT -1 ;\n');
  m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(10);
  m.type('RUN SPIN.FTH\n'); m.frames(30);
  m.press(PAD.A, 2, 0);
  m.frames(700);
  const status = m.lines().find((l) => /pass \d+|stopped/.test(l)) || '';
  const passes = +(status.match(/pass (\d+)/) || [0, 0])[1];
  check(passes > 300 && !status.includes('stopped'),
        `a NEXT that leaves a flag no longer fills the stack (${passes} passes)`);
}

console.log('sound');
{
  const m = new Machine();
  const heard = [];
  m.emu.onAudio = (s, rate) => heard.push({ s, rate });
  toConsole(m);
  m.type('440 500 BEEP 0 200 BEEP 880 300 BEEP ." queued" CR\n');
  check(m.text().includes('queued'), 'BEEP queues a note and returns');
  m.frames(90);
  const all = [];
  for (const h of heard) for (let i = 0; i < h.s.length; i += 2) all.push(h.s[i]);
  const rate = heard.length ? heard[0].rate : 0;
  // Pitch from the sign changes of each tone: two a cycle.
  const tones = [];
  let run = null;
  for (let i = 0; i < all.length; i++) {
    if (all[i] !== 0) {
      if (!run) run = { start: i, flips: 0, last: Math.sign(all[i]) };
      if (Math.sign(all[i]) !== run.last) { run.flips++; run.last = Math.sign(all[i]); }
      run.end = i;
    } else if (run && i - run.end > 200) { tones.push(run); run = null; }
  }
  if (run) tones.push(run);
  const hz = tones.map((t) => Math.round(t.flips / 2 / ((t.end - t.start) / rate)));
  const ms = tones.map((t) => Math.round((t.end - t.start) / rate * 1000));
  check(tones.length === 2, `two tones with a rest between (${tones.length})`);
  check(Math.abs(hz[0] - 440) < 6 && Math.abs(hz[1] - 880) < 10, `at 440 and 880 Hz (${hz})`);
  check(Math.abs(ms[0] - 500) < 15 && Math.abs(ms[1] - 300) < 15, `for 500 and 300 ms (${ms})`);
  m.type('QUIET 200 2000 BEEP SOUNDING? . QUIET SOUNDING? .\n');
  check(/-1 0/.test(m.text()), 'SOUNDING? and QUIET');
}

console.log('the pak changing under the system');
{
  const m = new Machine();
  toConsole(m);
  m.type('DIR\n'); m.frames(20);
  // The page adds a file to the pak, as dropping one on it does.
  pakfs.write(m.emu.pak, 'dropped.fth', new TextEncoder().encode('.( from the computer) CR\n'));
  m.type('INCLUDE DROPPED.FTH\n'); m.frames(30);
  check(m.text().includes('from the computer'), 'a file put on the pak from outside is seen');
  m.type('EDIT MINE.FTH\n'); m.frames(10); m.type('1\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(10);
  check(pakfs.list(m.emu.pak).map((f) => f.name).join() === 'dropped.fth,MINE.FTH',
        'and the system\'s own save keeps it (' + pakfs.list(m.emu.pak).map((f) => f.name) + ')');

  // Another pak, with its own files, pushed in without telling anyone.
  const other = new Uint8Array(32768);
  pakfs.write(other, 'OTHER.FTH', new TextEncoder().encode('1 2 +\n'));
  m.emu.pak = other;
  m.frames(70);                                  // it identifies once a second
  m.type('EDIT NEW.FTH\n'); m.frames(10); m.type('2\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(10);
  const names = pakfs.list(other).map((f) => f.name).join();
  check(pakfs.state(other) === 'ok' && names === 'NEW.FTH,OTHER.FTH',
        `saving to a swapped pak keeps what was on it (${names})`);
  check(new TextDecoder().decode(pakfs.read(other, pakfs.list(other)[1])) === '1 2 +\n',
        'with its contents intact');

  m.emu.pak = null;                              // pulled out
  m.frames(70);
  m.type('DIR\n'); m.frames(20);
  check(m.text().includes('RAM disk'), 'a pulled pak falls back to the RAM disk');
}

console.log('no Controller Pak');
{
  const m = new Machine(undefined, { pak: null });
  m.frames(90);
  check(m.text().includes('no Controller Pak'), 'the desktop says files are going to RAM');
  m.press(PAD.DOWN); m.press(PAD.A); m.frames(20);
  m.type('EDIT T.FTH\n'); m.frames(20);
  m.type('1 2 + .\n'); m.type(KEY.ctrl('s')); m.frames(10);
  check(m.text().includes('RAM disk'), 'saving says where it went');
  m.type(KEY.ESC); m.frames(20);
  m.type('INCLUDE T.FTH\n'); m.frames(30);
  check(/^\s*3\s*$/m.test(m.text()), 'and the file works from RAM');
}

console.log('a program that keeps its data: SKETCH.FTH');
{
  let m = new Machine();
  toConsole(m);
  m.type('RUN SKETCH.FTH\n'); m.frames(40);
  m.press(PAD.A); m.frames(60);
  check(m.text().includes('a new picture'), 'it opens as an app in a window');
  const grab = () => { const a = []; for (let y = 64; y < 304; y++) for (let x = 368; x < 624; x++) a.push(m.pixel(x, y)); return a; };
  const mv = (dx, dy) => { m.emu.mouse.dx = dx; m.emu.mouse.dy = -dy; m.frames(2); };
  mv(100, -40); m.emu.mouse.buttons = PAD.A;
  for (let i = 0; i < 30; i++) mv(3, 1);
  m.emu.mouse.buttons = 0; mv(-127, 0); mv(-127, 0);
  const painted = grab();
  const paper = painted[painted.length - 1];
  check(painted.filter((p) => p !== paper).length > 200, 'the mouse paints');
  m.press(PAD.Z, 4, 30);
  const pak = m.emu.pak;
  m = new Machine(undefined, { pak });
  toConsole(m);
  m.type('RUN SKETCH.FTH\n'); m.frames(40);
  m.press(PAD.A); m.frames(60);
  mv(-127, 0); mv(-127, 0);
  const again = grab();
  check(again.every((p, i) => p === painted[i]), 'the picture comes back after a power cycle');
  check(m.text().includes('loaded'), 'and says it loaded it');
}

console.log();
if (failures.length) {
  console.log(`${failures.length} check(s) failed:`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exit(1);
}
console.log('all checks passed');
