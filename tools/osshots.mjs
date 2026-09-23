// osshots.mjs -- the README's pictures, all of them, taken from the
// browser's emulator: the desktop, Files, the editor, the shell, the apps,
// the window manager, Tasks and the desk.
//     node tools/osshots.mjs
import { Machine, PAD, KEY } from './web.mjs';

const OUT = new URL('../docs/img/', import.meta.url).pathname;
const m = new Machine();
m.emu.mouseConnected = false;           // no pointer in the pictures
m.frames(90);

// A few files of one's own, written the way a person would.
m.press(PAD.DOWN); m.press(PAD.A); m.frames(20);
m.type('EDIT STARS.FTH\n'); m.frames(20);
m.type('\x16\\ stars.fth -- written on the N64, kept on the pak\n\n' +
       ': ROW ( n -- ) 0 DO 42 EMIT LOOP CR ;\n' +
       ': TRIANGLE ( n -- )\n   1+ 1 DO I ROW LOOP ;\n\n' +
       ': STARS\n   CYAN INK  8 TRIANGLE\n   WHITE INK ." and that is the lot" CR ;\n\nSTARS\n\x16');
m.type(KEY.ctrl('s')); m.frames(10);
m.type(KEY.UP); m.type(KEY.UP); m.type(KEY.UP); m.frames(5);
m.png(OUT + 'editor.png');
m.type(KEY.ESC); m.frames(20);
m.type('EDIT BOOT.FTH\n'); m.frames(20);
m.type(': HI  ." Good morning." CR ;  HI\n'); m.type(KEY.ctrl('s')); m.type(KEY.ESC); m.frames(20);
m.type('INCLUDE STARS.FTH\n'); m.frames(40);
m.type('DIR\n'); m.frames(40);
m.png(OUT + 'shell.png');
m.press(PAD.L); m.frames(30);
m.png(OUT + 'desktop.png');
m.press(PAD.UP); m.press(PAD.A); m.frames(30);
m.png(OUT + 'files.png');
m.press(PAD.B); m.frames(20);
m.press(PAD.DOWN); m.press(PAD.A); m.frames(20);
m.emu.mouseConnected = true;            // the sketch wants one
m.type('RUN SKETCH.FTH\n'); m.frames(90);
m.press(PAD.A); m.frames(40);
// The mouse counts up as positive, as the real one does.
const mv = (dx, dy) => { m.emu.mouse.dx = dx; m.emu.mouse.dy = -dy; m.frames(1); };
const stroke = (pts, fn) => {
  m.emu.mouse.buttons = PAD.A;
  for (let i = 0; i < pts; i++) mv(...fn(i));
  m.emu.mouse.buttons = 0; m.frames(2);
};
mv(100, -70);                                  // near the canvas's top left
m.press(PAD.CRIGHT, 2, 2); m.press(PAD.CRIGHT, 2, 2); m.press(PAD.CRIGHT, 2, 2);
m.press(PAD.CRIGHT, 2, 2); m.press(PAD.CRIGHT, 2, 2);     // cyan
stroke(96, (i) => [Math.round(6 * Math.cos(i / 15)), Math.round(6 * Math.sin(i / 15))]);
mv(60, 30);
m.press(PAD.CLEFT, 2, 2); m.press(PAD.CLEFT, 2, 2);       // amber
stroke(60, (i) => [i % 20 < 10 ? 3 : -3, 2]);
mv(-110, -40);
m.press(PAD.CLEFT, 2, 2);                                 // red
stroke(40, () => [3, 0]);
m.press(PAD.Z, 4, 20);
mv(127, 127); mv(127, 127); m.frames(4);
m.png(OUT + 'sketch.png');
// Three programs at once.
const t = new Machine();
t.emu.mouseConnected = false;
t.frames(90);
for (let i = 0; i < 7; i++) t.press(PAD.DOWN);
t.press(PAD.A); t.frames(420);
t.png(OUT + 'tasks.png');
// The desk: a prompt, with apps opened from it.
const d = new Machine();
d.emu.mouseConnected = false;
d.frames(90);
for (let i = 0; i < 8; i++) d.press(PAD.DOWN);
d.press(PAD.A); d.frames(60);
d.type('PAGE OPEN LIFE.FTH\n'); d.frames(30);
d.type('OPEN MANDEL.FTH\n'); d.frames(30);
d.type('OPEN NAVIER.FTH\n'); d.frames(30);
d.type(': SQUARES 6 1 DO I DUP * . LOOP ; SQUARES\n'); d.frames(200);
d.png(OUT + 'desk.png');
// Writing a program in a window, and running it at the prompt.
d.type('EDIT CUBE.FTH\n'); d.frames(20);
d.type('\x16\\ cube.fth -- written on the desk\n: CUBE ( n -- n^3 ) DUP DUP * * ;\n: CUBES 6 1 DO I CUBE . LOOP CR ;\nCUBES\n\x16');
d.type(KEY.ctrl('r')); d.frames(40);
d.png(OUT + 'desk-edit.png');
// The applications, each in its window, run until they have something to
// show: a picture finished, or a few passes of an animation.
function app(index, file, done) {
  const a = new Machine();
  a.emu.mouseConnected = false;
  a.frames(90);
  for (let i = 0; i < index; i++) a.press(PAD.DOWN);
  a.press(PAD.A); a.frames(60);
  a.press(PAD.A, 2, 0);
  for (let f = 0; f < 6000; f += 20) {
    a.frames(20);
    if (done(a.text())) break;
  }
  a.frames(2);
  a.png(OUT + file);
}
app(2, 'app-mandel.png', (t) => t.includes('drawn in'));
app(3, 'app-cornell.png', (t) => t.includes('drawn in'));
app(4, 'app-navier.png', (t) => /pass [3-9]/.test(t));
app(5, 'app-life.png', (t) => /pass [2-9]\d/.test(t));

// The window manager, as it opens.
const w = new Machine();
w.emu.mouseConnected = false;
w.frames(90);
for (let i = 0; i < 6; i++) w.press(PAD.DOWN);
w.press(PAD.A); w.frames(240);
w.png(OUT + 'wm.png');

console.log('docs/img: all of them');
