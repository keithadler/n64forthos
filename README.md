# n64forthos

A Forth operating system for the Nintendo 64. It boots from its own
cartridge, brings up 640×480 in sixteen bits a pixel, compiles the part of
itself that is written in Forth, and puts a desktop on the screen. It keeps
your files on the Controller Pak, has an editor to write programs in and a
shell to run them from, and runs your `BOOT.FTH` when it starts.

![the desktop](docs/img/desktop.png)

Nine things sit on that desktop. **Files** is everything on the Controller
Pak and in the ROM, to edit, run or delete. Four of them are **the Forth
source you are looking at**: the window shows the code, and the button beside
it runs that code into the canvas next to it.

![the Mandelbrot app](docs/img/app-mandel.png)
![the Cornell box](docs/img/app-cornell.png)

One of them is a **window manager, also written in Forth** — overlapping
windows you drag by their title bars, each one a canvas and a clip handed to
a Forth word once a frame.

![the window manager](docs/img/wm.png)

And the console is a real Forth prompt, and a shell. With nothing but a
controller you type on an on-screen keyboard; with a
[BlueRetro](https://blueretro.io) adapter the N64 takes a Bluetooth **mouse
and keyboard**, and the desktop grows a pointer while the prompt takes the
whole screen.

```bash
make            # build/n64forthos.z64, a 64 MiB cartridge image
make serve      # http://127.0.0.1:8795 -- run it in a browser, with your
                # own mouse and keyboard wired into it
make ostest     # the file system, editor and shell, headless: a minute
make test       # all of it: 208 Forth assertions and the system checks
make gui        # run it in mupen64plus
```

## An operating system, not only a language

**Files that outlive the power.** The only writable storage a stock N64 has
is the Controller Pak: 32 KiB of battery-backed memory in the back of the
controller, reached over the same serial bus as the buttons, 32 bytes at a
time, each transfer checked by two CRCs. [`src/pak.c`](src/pak.c) speaks that
protocol and [`src/fs.c`](src/fs.c) puts a file system on it: a table of 128
pages, a directory of 24 files, and saves that write the new copy into free
space before the directory points at it, so pulling the pak mid-save loses
the new version rather than the old one. With no pak in the controller the
same file system runs on a RAM disk, and every screen that saves says so.

The cartridge brings its own files -- the applications' source, a README, a
starter program -- on a read-only **ROM** volume. Names are looked up on the
pak first, so saving `MANDEL.FTH` after editing it gives you your own
Mandelbrot from the desktop, and deleting it gives you the original back.

Paks can be swapped with the power on. Before it trusts its copy of the
directory, the kernel reads the pak's header block and compares the
generation every save bumps; a different pak, or one written elsewhere, is
read again rather than written over, and a pulled pak drops the system back
to the RAM disk.

A pak with game saves on it is not taken over quietly: this is the system's
own format, not Nintendo's note table, and `FORMAT` explains that it erases
the saves before `ERASE-PAK` does it. A pak that is entirely blank is
formatted without asking, since there is nothing on it to lose.

![Files](docs/img/files.png)

**An editor.** [`src/edit.c`](src/edit.c) is a full-screen text editor with
line numbers, the arrows, Home/End/PgUp/PgDn, auto-indent, a click to place
the cursor, ^K to cut lines and ^U to put them back somewhere else, and ^F to
find (^G for the next). ^S saves, ^R saves and runs, Esc leaves (asking
first if there are changes). Without a keyboard it brings up the on-screen
one and the C buttons move the cursor.

![the editor](docs/img/editor.png)

**A shell, written in Forth.** `DIR CAT EDIT RUN INCLUDE DEL REN COPY FORMAT
MEM HELP` are words in [`src/system.fth`](src/system.fth) on top of kernel
words any program can use -- `LOAD-FILE SAVE-FILE DELETE-FILE RENAME-FILE
FILE? #FILES FILE#`, listed in [docs/WORDS.md](docs/WORDS.md). The prompt
takes the whole screen when there is a keyboard, keeps a history (up and
down), and gives way to the on-screen keyboard with R.

![the shell](docs/img/shell.png)

**Files, on the desktop.** The Files window lists both volumes with sizes and
free space: A edits, START runs, Z deletes (twice, to be sure), R starts a
new file. Running a file does what its contents ask for: one that defines
`ROWS` and `ROW` opens as an application in a window with its source beside
it, one that defines `FRAME` takes the screen, and anything else is included
at the prompt.

**Several programs at once.** **Tasks** ([`src/apps/tasks.fth`](src/apps/tasks.fth))
includes the window manager and then the Mandelbrot, Life and Navier-Stokes
files -- unchanged, the same ones the desktop opens one at a time -- and runs
all three side by side, each frame giving every window the next row of its
picture. Three apps that each define `ROW`, `START` and `NEXT` share one
dictionary because Forth binds a call when it compiles it: the moment a file
is in, its `ROW` is the newest `ROW`, and that is the one captured (with
`FIND-NAME`) before the next file can shadow it.

![three programs at once](docs/img/tasks.png)

**The window manager as a shell.** **Desk** ([`src/apps/desk.fth`](src/apps/desk.fth))
is a prompt in a window, with `OPEN LIFE.FTH` putting any app in a window
beside it -- while the prompt carries on. `EDIT` opens the editor in a
window of its own: write a program there, ^R runs it at the prompt below,
^O (or a click) moves the keys between the two, and the words it defined
are the prompt's to use. Everything else the prompt can do (`DIR`, `RUN`,
`FILES`, your own words) it does here. `RUN DESK.FTH` in a `BOOT.FTH` makes
it the machine's shell. Three things make it work:

- **A prompt and an editor that are one frame at a time**
  ([`src/repl.c`](src/repl.c) `repl_window_step`, [`src/edit.c`](src/edit.c)
  `edit_window_step`): the console and the editor can be anywhere now, and
  `CONSOLE` and `EDITOR` run one frame of them inside whatever window calls
  them. A window with another on top of it waits until it is uncovered
  rather than drawing into the one in front.
- **Stack floors.** A line typed there is evaluated from inside the running
  window manager, on stacks of its own stacked above the manager's; an
  error, or ^C, unwinds only that line, and what it leaves on the stack is
  kept for the next one as at any prompt. `INCLUDED` does the same for a
  file brought in by running code, which is how `OPEN` compiles an app
  without stopping the desk.
- **Namespaces, of a sort.** Once `OPEN` has captured an app's `ROW`,
  `ROWS`, `START` and `NEXT`, it resets the dictionary's search chain to
  where it was (`LATEST!`), so the app's words stay in memory and keep
  working but no longer stand in front of anything at the prompt --
  Mandelbrot's `DEPTH` and the window manager's `INK` do not shadow Forth's.
  The desk hides its own words the same way and shows the prompt `OPEN`.

![the desk](docs/img/desk.png)
![writing a program on the desk](docs/img/desk-edit.png)

**Programs that keep their data.** [`src/apps/sketch.fth`](src/apps/sketch.fth)
is a paint program in 90 lines of Forth that saves its picture as
`SKETCH.PIC` and finds it again after the power has been off.

![SKETCH.FTH](docs/img/sketch.png)

**Sound, in the background.** [`src/audio.c`](src/audio.c) drives the audio
interface: `440 500 BEEP` queues half a second of A and returns at once, and
the kernel synthesises the queue into three DMA buffers, topped up once a
frame from the one loop everything passes through, so a tune plays on while
you type, edit or run something else. `MUSIC.FTH` plays one; in the browser
it comes out of Web Audio.

**A fuller Forth.** `CREATE ... DOES>` for defining words, `?DO`, `+LOOP`,
a `LEAVE` that leaves, `CASE OF ENDOF ENDCASE`, `RECURSE`, `CHAR`, and
`ACCEPT` for programs that ask a question and wait for the answer.

**A break key.** `: SPIN BEGIN AGAIN ; SPIN` no longer means reaching for
the reset button: Esc or ^C, or START and Z on the controller, stops the
program -- and the rest of the file it came from -- and hands back the
prompt with the stacks cleared. Running code looks for the key every 32,768
times round a loop or through a helper, which costs the renderers under 2%.
The kernel reads only keyboards and controllers to do it, so a mouse's
movement and the button edges an app is waiting for are left alone. What it
cannot stop is a compiled loop made purely of inlined words (`BEGIN 1 DROP 0
UNTIL`), which never calls anything that could look.

**A boot script.** A `BOOT.FTH` on the pak runs at startup, before the
desktop, with its output in the boot log.

---

## What it is

Everything below runs on a **stock console**: 4 MiB of RDRAM with no
Expansion Pak, and a 64 MiB cartridge that is nothing but mask ROM.

**A cartridge that boots the way cartridges boot.** The PIF copies the first
0x1000 bytes into RSP data memory and jumps to 0xA4000040; that code —
[`src/ipl3.S`](src/ipl3.S), 72 bytes of it — puts the kernel in RDRAM and
enters it. [`src/entry.S`](src/entry.S) clears CP0 status, invalidates both
caches a line at a time, sets up a stack, zeroes `.bss` and calls `kmain`.
Exception vectors are installed, so a bad address in Forth paints a halt
screen with Cause, EPC and BadVAddr rather than wandering off.

**A Forth, not a Forth-flavoured interpreter.** Token threaded, dictionary in
RDRAM, 133 primitives, `:` compiles, `SEE` decompiles what is actually in
memory, `FORGET` rolls the dictionary back, and Forth addresses *are* machine
addresses — `@` and `!` reach the hardware.

```
ok> : SQUARE DUP * ;   7 SQUARE .
49
ok> SEE HELLO
: HELLO ." Hello, World!" CR ;
```

**A system written in itself.** [`src/system.fth`](src/system.fth) is
compiled at boot out of the ROM, a line at a time: the palette, the drawing
words, the window, the greeting. The applications are the same thing with
their own windows.

**A graphics layer where libultra would have been.** What Nintendo shipped as
a C library is here as words that take pixels on the stack:

```forth
: HELLO-WINDOW
   408 192 216 120 PANEL
   S" Hello, World!"  416 192 INKY  DRAW-TEXT
   S" a window drawn" 416 224 WHITE DRAW-TEXT ;
```

`PLOT BOX FRAME HLINE VLINE LINE DRAW-TEXT BLIT BLIT-SPRITE CLS FB RGB VSYNC
FRAMES`, plus 16.16 fixed point (`F* F/ FSQRT`) and `CANVAS-X/Y/W/H`, the
rectangle the desktop lends an application. The whole set is in
[docs/WORDS.md](docs/WORDS.md).

## The applications

Each one is asked for **one row per frame**, so the machine keeps reading its
controller while a picture paints, the window reports its progress, and B
stops a render half way without closing it.

**Mandelbrot** ([src/apps/mandel.fth](src/apps/mandel.fth)) — escape-time, 32
iterations, 16.16 fixed point, one sample per 2×2 block. A full canvas is
460M instructions: **4.9 seconds** of VR4300.

**Cornell box** ([src/apps/cornell.fth](src/apps/cornell.fth), 135 lines) — a
ray tracer: five walls, two spheres, one light and a shadow ray. Plane and
sphere intersections, normals, Lambert shading and shadow rays, all in fixed
point, all in Forth. About seven seconds a frame.

**Navier–Stokes** ([src/apps/navier.fth](src/apps/navier.fth)) — the
finite-time blowup, in the paper's own similarity variables: a slice through
the vortex with the speed as colour. Each pass drops τ by a fifth, so the
core radius (√τ) tightens and the speeds (τ^−3/4) grow while you watch. 70M
instructions a pass — **under a second**, so it animates. A Forth cousin of
[superfx-navier-stokes](https://github.com/keithadler/superfx-navier-stokes),
which did the same construction on a Super FX chip.

**Windows** ([src/apps/wm.fth](src/apps/wm.fth)) — a window manager, written
in Forth. The kernel draws rectangles, text and a pointer, and clips what it
is told to clip; what a window *is*, which one is in front, and what happens
when you drag a title bar are all up here in the language. A window is seven
cells — position, size, title, and the word that draws its contents — and
that word runs with the canvas and the clip set to the window's inside, so it
cannot draw anywhere else even if it tries.

![the window manager](docs/img/wm.png)

Repainting everything every frame would flicker, so the chrome is drawn when
something moves and only the contents are drawn between times, each inside
its own window.

One of the four demonstration windows computes: it renders a couple of rows
of an escape-time fractal a frame into its own canvas, notices when it has
been carried somewhere else and starts again, and the other windows go on
animating around it.

**Life** ([src/apps/life.fth](src/apps/life.fth)) — Conway's life, 64×64 on a
torus, two generations held in the dictionary. A generation a pass, and it
runs until you stop it.

![Life](docs/img/app-life.png)

**Files** -- every file on the pak and in ROM; edit, run, delete, new.

**Console** -- the prompt, on the on-screen keyboard or a real one.

**Devices** — what answered on each of the four joybus channels, live, and
the place to teach the system a real keyboard's key codes.

Each application is a file -- in ROM from [src/apps](src/apps), or your own
copy on the pak -- compiled into the dictionary when its window opens and
rolled back out of it when the window closes.

![the Navier-Stokes demo](docs/img/app-navier.png)

### Forth compiled to MIPS

A definition is compiled to machine code the moment it is finished, and
[`src/native.c`](src/native.c) walks the token thread the word already has
and writes instructions out. Nothing about the front end changes: the thread
stays where it was, which is what `SEE` decompiles and `FORGET` reclaims, and
a word the generator will not take is simply left interpreted. The boot log
says how many of each.

The convention is one register wide. A compiled word takes the Forth stack
pointer in `$a0`, returns it in `$v0` and keeps it in `$t8` while it runs;
the helpers in the kernel take and return it the same way, so a call costs
four instructions rather than a round trip through memory. The stack's two
limits sit in `$s1` and `$s2` and every move of the pointer is checked
against them; `@` and `!` check their address and alignment inline. Compiled
code is written through the uncached alias and the instruction cache is
invalidated over it — the part an emulator would never have made you do.

| | interpreted | compiled | a full frame |
| --- | --- | --- | --- |
| Cornell box | 18,769 instructions a pixel | **2,719** — 6.9× | 1.5 s |
| Navier–Stokes | 1,548 | **498** — 3.1× | 0.3 s a pass |
| Mandelbrot | 4,278 | **1,416** — 3.0× | 0.8 s |

Thirty-odd words are inlined rather than called — the stack shuffles, the
arithmetic and comparisons, `@` and `!` with their checks, the shifts, `MIN`
and `MAX`, `>R` and `R>` straight onto the return stack, and `F*` through the
64-bit product. Everything else is a call to the same primitive the
interpreter runs.

Three of the bugs in getting here are worth keeping: a stack check at word
entry cannot be sound across a loop; `lui`/`lw` sign-extends its offset, so
the upper half needs rounding up; and a branch offset counts from *after* the
delay slot, which made an inlined `MIN` return the larger of the two. The
last one is the reason [`test/tests.fth`](test/tests.fth) now runs its
arithmetic twice — once interpreted, once inside definitions, because only
the second is compiled and the two have to agree. 139 assertions.

### What optimisation actually bought

Measured, not guessed — `tools/bench.py` counts instructions a pixel and
`tools/profile.py` samples the program counter:

| change | effect |
| --- | --- |
| one ray per 2×2 block, in both renderers | **4× faster** |
| a row per frame instead of a whole frame | the UI stays alive |
| `-Os` to `-O2` | **1.4× faster** |
| `2DUP`/`2DROP` as primitives rather than Forth | 1% |
| `F/` rewritten to use the CPU's divider | 1.3% |
| a fast path for hot words in the inner interpreter | **nothing** — `prim()` was already inlined; reverted |
| `-O3` | **slower**, and bigger; reverted |
| the RDP filling rectangles instead of the CPU | **2.4×** on the interface |
| compiling definitions to MIPS | **3–7×** on the applications |

The floor used to be the interpreter itself, at about 36 instructions per
Forth word. Both ways past it are now taken: the RDP does the fills, and
definitions are compiled. What is left is the RSP, which is eight lanes of
16-bit arithmetic sitting idle — and microcode is a different kind of
project, because the applications would stop being Forth.

## Mouse and keyboard

The kernel identifies each joybus channel once a second and reads whatever is
there: a controller (`0x0500`), an N64 Mouse (`0x0200`) or a Randnet Keyboard
(`0x0002`) — which is exactly the set a BlueRetro adapter can present over
Bluetooth. The mouse moves a pointer the desktop draws and rubs out itself;
its buttons click the launcher, the RUN and CLOSE buttons, and the keys of
the on-screen keyboard.

The keyboard is the honest weak spot: the transport is implemented, but the
**Randnet key codes are from documentation and have never met the real
keyboard**. So the system does not pretend. The Devices window shows the raw
code of whatever you press, and walks you through the alphabet building a
table, which it prints as Forth you can paste into `system.fth`:

```
ok> 42 65 KEY!   ( A )
```

Our own emulator sends ASCII down the wire, which the default table maps
straight through — so in the browser and in the tests, typing simply works.

## In a browser, with your own mouse and keyboard

`make serve` puts it at <http://127.0.0.1:8795>, running in
[`web/n64emu.js`](web/n64emu.js) -- the same interpreter as the test harness,
ported to the browser precisely so the page can hand the OS real input: your
mouse arrives as an N64 Mouse on channel 2 and your keyboard as a Randnet
Keyboard on channel 3. Click the picture to give it the mouse; type at the
prompt, or paste a whole program in.

The controller has a Controller Pak in it, emulated down to both CRCs, and
the page keeps its 32 KiB in the browser's local storage: a reload is a
power cycle with the same pak still inserted. Below the picture you can
eject it (the system falls back to a RAM disk), download it as a 32 KiB
image, or load one. **files…** lists what is on it, each with a download
button, and files dropped on the picture go onto the pak
([`web/pakfs.js`](web/pakfs.js) speaks the same format), so programs move
between the N64 and your computer either way.

It runs at 35–60M instructions a second in a normal window, against the
VR4300's 93.75M — half speed, and the desktop is idle most of the time
anyway.

## Real hardware

Not yet tested on a console. [docs/HARDWARE.md](docs/HARDWARE.md) is the
practical guide — what to put on the cart, what you should see and in what
order, what each failure would mean, how to set up a BlueRetro adapter, and
how to teach the system a real keyboard. In short, two things stand between
here and a flashcart, and both are known:

1. **RDRAM is not initialised.** Emulators hand it to you working. On
   hardware the boot block has to configure the RI and the RDRAM modules
   before anything can be written to memory.
2. **The PIF checks the boot block against the CIC**, and only Nintendo's
   IPL3 — or a deliberate checksum collision like the free one in
   [libdragon](https://github.com/DragonMinded/libdragon) — passes.

Both are solved by an IPL3 that does the work, so the build takes one:

```bash
make IPL3=path/to/ipl3.bin      # a 0xFB8-byte boot block that isn't ours
```

The kernel itself asks nothing of the boot block beyond being at 0x80000400
with RDRAM working, and `build/kernel.elf` is there for a loader that would
rather have an ELF.

## How it is tested

`make ostest` drives the browser's emulator from Node
([`tools/ostest.mjs`](tools/ostest.mjs)): it runs the Forth suite twice, on a
Controller Pak and on the RAM disk; writes a program in the editor, saves it,
runs it, power-cycles with the same pak and finds it again; checks that
`BOOT.FTH` runs at power on; puts a file on the pak from outside and swaps
in another pak mid-session, checking that neither is written over; works
the shell (`COPY REN INCLUDE DEL`, the ROM
refusing to be deleted, `FORMAT` warning first, the history); drives Files
to run and delete; stops runaway loops with the break key; answers a program's `ACCEPT`; measures the pitch and
length of queued notes from the samples the audio interface was given; and
paints in `SKETCH.FTH` with the mouse, saves, powers off and checks the
picture comes back pixel for pixel.

`make test` runs that, then boots two cartridges on the Python emulator with
no window and no console:

**A test cartridge** runs [`test/tests.fth`](test/tests.fth) at boot and
leaves the pass and fail counts in RDRAM, where
[`tools/check.py`](tools/check.py) reads them back out of the emulator. 208
assertions -- including defining words, `?DO`, `+LOOP`, `LEAVE` and `CASE`, saving, loading, renaming and deleting files, a ROM
file refusing to be deleted, a pak copy standing in for a ROM one, and
`INCLUDE` of a file written by Forth -- thirteen of them deliberate errors — divide by zero, unaligned
store, an `IF` that never closes, a dictionary overflow, a stack overflow —
each followed by assertions that the system still computes and still has an
empty stack.

**The real cartridge** is driven like a person would drive it: press A, move
the pointer, click, type. Thirty-odd checks cover the boot console, the
prompt with both keyboards, and both renderers -- including that the Cornell
box comes out red on the left and green on the right. Its console is checked by *decoding the framebuffer
back to text* — the kernel draws exact 8×16 glyphs, so
[`tools/screen.py`](tools/screen.py) matches each cell against the same font
and turns the screen into eighty columns of characters. "The console says
Hello, World!" is then a string comparison covering boot block, kernel,
Forth, console and video interface.

The emulator underneath is [`tools/n64emu.py`](tools/n64emu.py), written for
this repo. It boots from the cartridge header, so the boot block is under
test too, implements the integer MIPS subset this kernel uses plus the VI,
PI and SI registers it touches, and *raises* on anything it does not
implement rather than quietly doing the wrong thing. Controllers, mice and
keyboards are emulated on the joybus, which is how the tests can type.

## Robustness

The kernel is written to survive its user, because its user is a prompt.

- The dictionary has a limit and every allocation checks it.
- A definition that fails is unlinked and its space reclaimed: `: BADX IF ;`
  leaves `HERE` exactly where it was.
- Control flow compiles on its own stack, so `LOOP` without `DO` is a message
  rather than a corrupt thread.
- `@ ! C@ C! +! FILL TYPE DUMP` check range and alignment; a bad address is
  refused, not faulted.
- Stacks are bounded and report overflow and underflow.
- Source is read a line at a time: an error costs you the line, and the line
  it was on is printed under the message.
- `FORGET` will not eat the kernel's own words.

## Layout

```
src/ipl3.S      the boot block the PIF runs out of RSP DMEM
src/entry.S     kernel entry: caches, stack, .bss, exception landing pad
src/kernel.c    bring-up, boot log, the handover to the desktop
src/video.c     the video interface: 640x480, 16bpp, interlaced NTSC
src/gfx.c       every routine that touches a pixel, including the pointer
src/console.c   text in its own columns, with a status bar
src/input.c     joybus: controllers, mice, keyboards, and the typeahead
src/pak.c       the Controller Pak: 32-byte transfers and their CRCs
src/audio.c     sound: a queue of notes, fed to the audio interface
src/fs.c        the file system: ROM, and the pak (or a RAM disk)
src/edit.c      the text editor
src/files.c     the Files window
src/osk.c       the on-screen keyboard
src/repl.c      the prompt, with history
src/desktop.c   the launcher, the application windows, the devices window
src/forth.c     dictionary, inner interpreter, compiler, primitives
src/system.fth  the part of the system written in Forth
src/apps/*     the ROM volume: the applications, which are also their own
                source listing, HELLO.FTH, SKETCH.FTH, MUSIC.FTH, README.TXT
test/tests.fth  the test suite, also written in Forth
tools/          font, boot source packer, cartridge builder, emulator, checks;
                web.mjs and ostest.mjs drive the browser's emulator from Node
web/            the page, and the emulator ported to JavaScript
```

Built with Homebrew's LLVM, which targets big-endian MIPS out of the box, and
lld. There is no cross-gcc to build.

## Where this got to

Everything described above works, is tested, and is in this repository. The
system boots, compiles itself, runs applications written in its own language,
takes a mouse and a keyboard, manages windows, and keeps files: you can write
a program on the machine, save it to the Controller Pak, turn the power off,
and run it again tomorrow. Rendering is three to
seven times faster than where it started, and the interface is no longer the
part that costs anything: a full screen rebuild is about ten milliseconds,
and an idle frame touches a few hundred pixels.

Three things were deliberately left for later, and none of them is hiding:

**A console has not seen it yet**, and neither has a real Controller Pak.
The pak protocol and both of its CRCs are implemented from the documented
format and checked against two emulators written from the same documents,
which is not the same as a pak. The boot block does not initialise RDRAM,
and the PIF checksums it against the CIC — both solved by handing
`make IPL3=...` a boot block that does the work.
[docs/HARDWARE.md](docs/HARDWARE.md) is the guide for the day the hardware
arrives, including what each failure would mean and how to teach the system a
real Randnet keyboard, whose codes are still unverified.

**The RSP is idle.** Eight lanes of sixteen-bit arithmetic, and the
applications spend all their time on exactly the sort of maths it exists for.
It is the last large multiplier, and taking it would mean the applications
stop being Forth you can read in the window beside the picture — which is why
it was not taken.

**Files is still full-screen.** The desk runs the prompt, the editor and the
applications in windows, and brings Files up over the whole screen; making
it a window as well is the step after. And the desk is cooperative: a line that runs for
a long time holds everything else up until it finishes or is broken.

## Licence

MIT.
