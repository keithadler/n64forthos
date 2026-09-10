# n64forthos

A Forth operating system for the Nintendo 64. It boots from its own
cartridge, brings up 640×480 in sixteen bits a pixel, compiles the part of
itself that is written in Forth, and puts a desktop on the screen.

![the desktop](docs/img/desktop.png)

Two applications ship with it, and both are **the Forth source you are
looking at**: the window shows the code, and the button next to it runs that
code into the canvas beside it.

![the Mandelbrot app](docs/img/app-mandel.png)
![the Cornell box](docs/img/app-cornell.png)

The console is a real Forth prompt. With nothing but a controller you type on
an on-screen keyboard; with a [BlueRetro](https://blueretro.io) adapter the
N64 takes a Bluetooth **mouse and keyboard**, and the desktop grows a pointer
and the prompt takes dictation.

```bash
make            # build/n64forthos.z64, a 64 MiB cartridge image
make serve      # http://127.0.0.1:8795 -- run it in a browser, with your
                # own mouse and keyboard wired into it
make test       # boot it headless: 102 Forth assertions and 30 system checks
make gui        # run it in mupen64plus
```

---

## What it is

**A cartridge that boots the way cartridges boot.** The PIF copies the first
0x1000 bytes into RSP data memory and jumps to 0xA4000040; that code —
[`src/ipl3.S`](src/ipl3.S), 72 bytes of it — puts the kernel in RDRAM and
enters it. [`src/entry.S`](src/entry.S) clears CP0 status, invalidates both
caches a line at a time, sets up a stack, zeroes `.bss` and calls `kmain`.
Exception vectors are installed, so a bad address in Forth paints a halt
screen with Cause, EPC and BadVAddr rather than wandering off.

**A Forth, not a Forth-flavoured interpreter.** Token threaded, dictionary in
RDRAM, 130 primitives, `:` compiles, `SEE` decompiles what is actually in
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

**Mandelbrot** ([src/apps/mandel.fth](src/apps/mandel.fth), 45 lines) —
escape-time, 32 iterations, 16.16 fixed point, 256×256. About 7,100
instructions a pixel, so a full canvas is roughly five seconds of VR4300.

**Cornell box** ([src/apps/cornell.fth](src/apps/cornell.fth), 135 lines) — a
ray tracer: five walls, two spheres, one light and a shadow ray, at one ray
per 2×2 block. Plane and sphere intersections, normals, Lambert shading and
shadow rays, all in fixed point, all in Forth.

**Console** — the prompt, on the on-screen keyboard or a real one.

**Devices** — what answered on each of the four joybus channels, live, and
the place to teach the system a real keyboard's key codes.

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
[`web/n64emu.js`](web/n64emu.js) — the same interpreter as the test harness,
ported to the browser precisely so the page can hand the OS real input: your
mouse arrives as an N64 Mouse on channel 2 and your keyboard as a Randnet
Keyboard on channel 3. Click the picture to give it the mouse; type at the
prompt.

It runs at 35–60M instructions a second in a normal window, against the
VR4300's 93.75M — half speed, and the desktop is idle most of the time
anyway.

## Real hardware

Not yet tested on a console; two things stand between here and a flashcart,
and both are known:

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

`make test` boots two cartridges with no window and no console:

**A test cartridge** runs [`test/tests.fth`](test/tests.fth) at boot and
leaves the pass and fail counts in RDRAM, where
[`tools/check.py`](tools/check.py) reads them back out of the emulator. 102
assertions, thirteen of them deliberate errors — divide by zero, unaligned
store, an `IF` that never closes, a dictionary overflow, a stack overflow —
each followed by assertions that the system still computes and still has an
empty stack.

**The real cartridge** is driven like a person would drive it: press A, move
the pointer, click, type. Its console is checked by *decoding the framebuffer
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
src/input.c     joybus: controllers, mice, keyboards
src/repl.c      the prompt and the on-screen keyboard
src/desktop.c   the launcher, the application windows, the devices window
src/forth.c     dictionary, inner interpreter, compiler, primitives
src/system.fth  the part of the system written in Forth
src/apps/*.fth  the applications, which are also their own source listing
test/tests.fth  the test suite, also written in Forth
tools/          font, boot source packer, cartridge builder, emulator, checks
web/            the page, and the emulator ported to JavaScript
```

Built with Homebrew's LLVM, which targets big-endian MIPS out of the box, and
lld. There is no cross-gcc to build.

## Licence

MIT.
