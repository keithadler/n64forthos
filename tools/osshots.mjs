// osshots.mjs -- the README's pictures of the operating system: the desktop,
// Files, the editor and SKETCH.FTH, taken from the browser's emulator.
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
console.log('docs/img: editor.png shell.png desktop.png files.png sketch.png tasks.png');
