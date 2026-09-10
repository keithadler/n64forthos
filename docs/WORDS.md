# The word set

122 words at boot from the kernel, plus what
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
DO LOOP  I  J
```

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

## Controllers

```
POLL  PAD  PAD?  STICK-X  STICK-Y
```

## Test support

```
REPORT  ( passes fails -- )
```

Writes a result block to 0x80380000 where a debugger — or `tools/check.py`,
reading RDRAM out of the emulator — can find it without a screen.

## Added by system.fth

```
2DUP  2DROP  ?  NOT  SQUARED  STAR  STARS  BL  TRUE  FALSE
CYAN  AMBER  WHITE  GREY  INKY  SLATE  SCREEN-W  SCREEN-H
BARS  PANEL  HELLO-WINDOW  HELLO  GREET
```
