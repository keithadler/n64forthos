# Running it on a real Nintendo 64

Nothing here has met a console yet. This is what to try, in what order, and
what each failure would mean — written so that a black screen is diagnosable
rather than mysterious.

## What to put on the cart

```bash
make                    # build/n64forthos.z64, 64 MiB
```

That file is the cartridge: plain mask ROM, no save chip, no expansion pak,
no coprocessor. A stock console with 4 MiB of RDRAM is all it wants — the
framebuffer sits at 2 MiB and the dictionary at 3 MiB, so nothing reaches
past the fourth.

## What you should see, in order

1. **The screen goes dark navy, immediately.** The kernel programs the video
   interface and clears the framebuffer before it does anything else, so this
   is the first sign of life. A black screen means the kernel never ran.
2. **A boot log**, seven lines, ending with the word count and a `PAK` line
   saying what is in controller 1's accessory slot.
3. **`Hello, World!`** in cyan, from `GREET` in `system.fth`.
4. **The desktop**, listing nine entries, Files first, with a line at the
   foot saying where your files are going.

If you get (1) but not (2), the kernel started and something after the video
interface hung — the boot log lines will tell you how far. If you get nothing
at all, it is the boot block, and that is the interesting case:

## The two things that might stop it

**RDRAM initialisation.** On a real console it is IPL3 that configures the RI
and the RDRAM modules; emulators hand you working memory. Our boot block
(`src/ipl3.S`) does not do it. Booting from a flashcart menu usually means
RDRAM is already up — the menu had to use it — so this may never bite. From
a cold boot with the cartridge in, it will.

**The CIC check.** At power-on the PIF checksums the 4 KiB boot block and
compares it with what the CIC expects, and only Nintendo's own IPL3 matches.
Flashcarts emulate the CIC, and their emulation generally accepts whatever
checksum arrives, which is why homebrew with a custom boot block runs on
them. On a real cartridge with a real CIC, it would not.

Both are solved by a boot block that does the work, and the build takes one:

```bash
make IPL3=path/to/ipl3.bin
```

[libdragon](https://github.com/DragonMinded/libdragon)'s IPL3 is free, does
the RDRAM initialisation, and was deliberately made to collide with the
CIC-6102 checksum. It expects to load an ELF; `build/kernel.elf` is there.

The kernel itself asks the boot block for very little: put the image at
0x80000400 with RDRAM working, and enter it. It empties both caches itself
and only then switches from uncached to cached execution, which is the part
that matters at power-on when the instruction cache holds whatever it holds.

## Mouse and keyboard

With a [BlueRetro](https://blueretro.io) adapter:

1. Pair the adapter, open `blueretro.io` in a Chromium browser, and set the
   input mode preset: **Default Mouse** for a mouse, **Default
   Gamepad/Keyboard** for a keyboard.
2. Pair the mouse or keyboard to the adapter.
3. Boot the cartridge and open **Devices**. Each of the four joybus channels
   reports what answered it: controller, mouse, keyboard, or nothing. This is
   the fastest way to tell whether the adapter is presenting what you think.

The mouse should move a pointer immediately: the kernel identifies an N64
Mouse (`0x0200`) and reads its relative movement where a controller reports
its stick.

## Teaching it the keyboard

The Randnet key codes in this kernel are from documentation and have never
been checked against the real keyboard, so the system does not assume them.

1. Open **Devices**. Press a key: the raw code appears, with what it maps to.
2. Press **A** to start learning. It asks for each character in turn; press
   the key, and it binds the code you sent to the character it asked for.
3. Every binding is printed to the console as Forth:

   ```
   42 65 KEY!   ( A )
   ```

4. Paste those lines into `src/system.fth` and rebuild, and the keyboard
   works from boot. (Or type them at the prompt to try them out first.)

If the raw codes turn out to be plain ASCII after all, nothing needs
teaching: that is what the default table already assumes, because it is what
our own emulator sends.

## The Controller Pak

Files go on the Controller Pak in controller 1, so put one in before
powering on (or insert one later and open Files, which looks again). The
`PAK` line in the boot log says what the kernel found:

- **`none: files go on a RAM disk`** -- no pak answered. Everything still
  works; it is gone at power off.
- **`a blank pak: formatted it`** -- an all-zero pak, formatted without
  asking because there was nothing on it.
- **`not formatted: FORMAT to use it`** -- the usual case for a pak that has
  held game saves. `FORMAT` at the prompt explains, `ERASE-PAK` does it, and
  the saves are gone. Use a pak you do not mind losing.
- **`not answering`** -- a pak said it was there but reads failed their CRC
  three times running. That would be the interesting report: the address and
  data CRCs in `src/pak.c` are from the documented protocol and have only
  met emulators.

Then: save a file, power off, power on, and `DIR`. The pak is the whole
point of the exercise.

## Sound

`RUN MUSIC.FTH` should play a tune through the television. The audio
interface is set for 22,050 samples a second from the NTSC clock; on a PAL
console the pitch would be a little off (the clock is different), and the
kernel does not yet ask which it is running on.

## What would be worth reporting back

- Whether it boots from your flashcart's menu, and which cart it is.
- Whether a file saved to a real Controller Pak is still there after a power
  cycle, and which pak it was (first-party or third-party).
- Whether `MUSIC.FTH` plays cleanly, or crackles between buffers.
- The four channel identifications in **Devices** with the adapter attached.
- The raw key codes for a few known keys — those settle the Randnet table
  for good.
- How long `Mandelbrot` takes: it should be about five seconds, and the
  window says how many frames it took.
