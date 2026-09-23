# The word set

169 words from the kernel, plus what
[`src/system.fth`](../src/system.fth) adds on top. `WORDS` lists them on the
console; `SEE name` decompiles any colon definition.

A cell is 32 bits. Addresses are real machine addresses.

## Stack

```
DUP  ?DUP  DROP  SWAP  OVER  ROT  NIP  TUCK  PICK  ROLL  DEPTH
>R  R>  R@
```

## Arithmetic and logic

```
+  -  *  /  MOD  NEGATE  ABS  MIN  MAX  1+  1-  2*  2/
AND  OR  XOR  INVERT  LSHIFT  RSHIFT
=  <>  <  >  U<  0=  0<  0>
```

## Memory

```
@  !  C@  C!  +!  FILL  HERE  ALLOT  ,  C,  CELLS  CELL+  DUMP
```

Every one of these checks that the address is inside RDRAM or the hardware
register block, and that it is aligned. A bad address is an error message,
not an exception.

## Defining and compiling

```
:  ;  IMMEDIATE  VARIABLE  CONSTANT  LITERAL  [  ]  '  EXECUTE  FORGET
(  \  ."  S"
```

`FORGET name` rolls the dictionary back to just before that word, and refuses
to touch anything the kernel defined.

## Control flow

```
IF ELSE THEN     BEGIN UNTIL     BEGIN AGAIN     BEGIN WHILE REPEAT
DO LOOP  ?DO  +LOOP  I  J  LEAVE  UNLOOP  RECURSE
CASE  OF  ENDOF  ENDCASE
```

`?DO` skips the loop when the limit and the start are equal; `+LOOP` steps by
the number on the stack and stops when the index crosses the boundary between
limit-1 and limit, either way. A definition that uses `?DO`, `+LOOP`,
`CREATE` or `DOES>` stays interpreted rather than being compiled to MIPS.

## Defining words

```forth
: CONST  CREATE ,  DOES> @ ;
42 CONST ANSWER            \ ANSWER leaves 42
: ARRAY  CREATE CELLS ALLOT  DOES> SWAP CELLS + ;
10 ARRAY SCORES            \ 3 SCORES leaves the address of the fourth
```

`CREATE name` makes a word that leaves the address of the space after it;
`DOES>` gives the word just created something to do with that address.
`' name >BODY` is the same address. `CHAR x` and `[CHAR] x` give a
character's code.

Definitions can see their own name, so recursion needs no extra word:

```forth
: FACT DUP 1 > IF DUP 1- FACT * ELSE DROP 1 THEN ;
```

## Console

```
.  U.  H.  .S  EMIT  CR  SPACE  SPACES  TYPE  PAGE  AT  INK
DECIMAL  HEX  BASE  WORDS  SEE  ABORT
```

`AT ( row col -- )` places the cursor; `INK ( colour -- )` sets the pen.

## Graphics

The abstraction over what the hardware gives you: a framebuffer and the
video interface.

| word | stack | what it does |
| --- | --- | --- |
| `FB` | `( -- addr )` | the framebuffer, uncached |
| `RGB` | `( r g b -- c )` | 8-bit components to a 5551 pixel |
| `CLS` | `( c -- )` | fill the screen |
| `PLOT` | `( x y c -- )` | one pixel |
| `BOX` | `( x y w h c -- )` | filled rectangle |
| `FRAME` | `( x y w h c -- )` | rectangle outline |
| `HLINE` | `( x y w c -- )` | horizontal run |
| `VLINE` | `( x y h c -- )` | vertical run |
| `LINE` | `( x1 y1 x2 y2 c -- )` | Bresenham line |
| `DRAW-TEXT` | `( addr len x y c -- )` | 8×16 text at pixel coordinates |
| `BLIT` | `( addr x y w h -- )` | copy 16-bit pixels |
| `BLIT-SPRITE` | `( addr x y w h -- )` | the same, treating alpha-clear pixels as transparent |
| `VSYNC` | `( -- )` | wait for vertical blank |
| `FRAMES` | `( -- n )` | frames since boot |

Strings for `DRAW-TEXT` come from `S"`:

```forth
S" Hello, World!" 336 256 WHITE DRAW-TEXT
```

## Fixed point

A cell holds a 16.16 fixed-point number in the applications: 65536 is 1.0.

| word | stack | what it does |
| --- | --- | --- |
| `F*` | `( a b -- ab )` | multiply, through a 64-bit intermediate |
| `F/` | `( a b -- a/b )` | divide, likewise |
| `FSQRT` | `( a -- root )` | square root |

The VR4300 multiplies 32x32 into 64 for nothing, but there is no library
under this kernel to divide a 64-bit value, so `F/` and `FSQRT` are written
out longhand in `src/forth.c`.

## The canvas

An application does not own the screen; the desktop lends it a rectangle.

```
CANVAS-X  CANVAS-Y  CANVAS-W  CANVAS-H
```

They are constants for as long as your window is open, which is why an app
can say `CANVAS-W 2/ CONSTANT RW` at compile time and mean it.

## Input

| word | stack | what it does |
| --- | --- | --- |
| `POLL` | `( -- )` | read every joybus channel now |
| `BUTTONS` | `( -- mask )` | what controller 1 is holding |
| `PRESSED` | `( -- mask )` | what went down since the last poll |
| `KEY` | `( -- c )` | wait for a key and return it |
| `INKEY` | `( -- c \| 0 )` | a key if one was pressed, without waiting |
| `KEY!` | `( code ascii -- )` | teach the keyboard table one key |
| `MOUSE-X` `MOUSE-Y` | `( -- n )` | where the pointer is |
| `MOUSE-B` `MOUSE-HIT` | `( -- mask )` | buttons held, and newly pressed |
| `CURSOR` `HIDE-CURSOR` | `( -- )` | draw or rub out the pointer |
| `MS` | `( n -- )` | wait n milliseconds, to the nearest frame |
| `ACCEPT` | `( addr max -- n )` | read a line typed on the keyboard, with rub out |

Keys are queued as they go down, so a program asking with `KEY` later still
gets one pressed while it was busy. Besides ASCII, the editing keys arrive as
codes 128 (up) to 136 (delete); see `src/n64.h`.

## Sound

| word | stack | what it does |
| --- | --- | --- |
| `BEEP` | `( hz ms -- )` | queue a note (0 Hz is a rest) and carry on |
| `QUIET` | `( -- )` | stop, and forget what is queued |
| `SOUNDING?` | `( -- flag )` | is anything still to play |
| `VOLUME` | `( percent -- )` | 0 to 100; 25 to start with |

Notes are square waves at 22,050 samples a second, queued up to 127 deep
(`BEEP` waits only when the queue is full) and fed to the audio interface
from the frame wait, so they play on under whatever runs next.

## Files

Two volumes: **ROM**, the files the cartridge was built with, which cannot be
changed; and **PAK**, the Controller Pak in controller 1 -- or, with no pak,
a RAM disk that is gone at power off. A name is looked up on the pak first,
so a copy saved there stands in for the ROM file of the same name. Names are
1 to 19 characters with no spaces, and compare without regard to case.

| word | stack | what it does |
| --- | --- | --- |
| `LOAD-FILE` | `( addr max c-addr u -- n ior )` | read up to max bytes of a file |
| `SAVE-FILE` | `( addr n c-addr u -- ior )` | write a whole file, replacing any |
| `DELETE-FILE` | `( c-addr u -- ior )` | remove one from the pak |
| `RENAME-FILE` | `( c-addr1 u1 c-addr2 u2 -- ior )` | rename one on the pak |
| `FILE?` | `( c-addr u -- size vol -1 \| 0 )` | does it exist, how big, where |
| `#FILES` | `( -- n )` | how many files there are, both volumes |
| `FILE#` | `( i -- c-addr u size vol )` | the i-th, pak files by name first |
| `DISK-FREE` | `( -- bytes )` | room left on the pak |
| `DISK-STATE` | `( -- n )` | 1 pak, 2 RAM disk, 3 unformatted, 4 damaged, 5 no answer |
| `MOUNT` | `( -- n )` | look at the controller again, after a pak swap |
| `(FORMAT)` | `( -- ior )` | format the pak, no questions asked |
| `.IOR` | `( ior -- )` | say what an ior means, if it is not 0 |
| `.VOL` | `( vol -- )` | PAK, RAM or ROM |
| `PARSE-NAME` | `( -- c-addr u )` | the next word of the line being read |
| `FIND-NAME` | `( c-addr u -- xt \| 0 )` | the newest word by that name, if any |
| `CMOVE` | `( from to n -- )` | copy bytes |
| `UNUSED` | `( -- n )` | dictionary bytes left |

An ior is 0 for success and negative otherwise: -1 no such file, -2 no room,
-3 directory full, -4 too big, -5 read-only (ROM), -6 bad name, -7 exists,
-8 the pak did not answer, -9 not formatted, -10 damaged.

Three words run other source, so they work at the prompt (or in a file being
included) but not inside a definition:

| word | what it does |
| --- | --- |
| `INCLUDE name` | compile and run a file, a line at a time |
| `EDIT name` | open the editor on it; a new name starts a new file |
| `RUN name` | as the desktop would: an app gets its window, anything else is included |

## Test support

```
REPORT  ( passes fails -- )
```

Writes a result block to 0x80380000 where a debugger — or `tools/check.py`,
reading RDRAM out of the emulator — can find it without a screen.

## Added by system.fth

```
?  NOT  SQUARED  STAR  STARS  BL  TRUE  FALSE
-ROT  /MOD  */  0<>  ERASE  WITHIN
CYAN  AMBER  WHITE  GREY  INKY  SLATE  SCREEN-W  SCREEN-H
BARS  PANEL  HELLO-WINDOW  HELLO  GREET
```

And the shell, written in Forth on top of the file words:

```
DIR  LS  CAT  DEL  REN  COPY  FORMAT  ERASE-PAK  MEM  HELP
.R  TYPE-FILE  FBUF
```

`FORMAT` only explains; `ERASE-PAK` does it.
