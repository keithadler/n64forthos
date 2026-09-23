\ sketch.fth -- paint with the mouse, and keep the picture.
\
\ A program that uses files: the picture is saved on the
\ Controller Pak as SKETCH.PIC, so it is still here after the
\ power has been off, and it comes back when you run this.
\
\ Left button paints, right button rubs out, and a click on the
\ bar picks a colour (so do C-left and C-right).
\ Z saves, R loads the saved one, L clears.

64 CONSTANT GW                      \ the grid, in cells
60 CONSTANT GH
GW GH * CONSTANT AREA
4 CONSTANT CELL                     \ pixels to a cell
GH CELL * CONSTANT BAR-Y            \ the colour bar, under it
1 CONSTANT ROWS                     \ one row a frame, for ever

VARIABLE PIC  AREA ALLOT            \ a byte a cell: the colour
VARIABLE INKC                       \ the colour in hand

VARIABLE PALETTE  7 CELLS ALLOT
: COLOUR! ( r g b n -- ) >R RGB R> CELLS PALETTE + ! ;
  8  10  30 0 COLOUR!               \ the paper
235 240 250 1 COLOUR!
255  90  90 2 COLOUR!
255 190  90 3 COLOUR!
250 230  80 4 COLOUR!
 90 210 120 5 COLOUR!
 96 224 255 6 COLOUR!
170 120 255 7 COLOUR!
: COLOUR ( n -- pixel ) 7 AND CELLS PALETTE + @ ;

: CELL@ ( x y -- n ) GW * + PIC + C@ ;
: CELL! ( n x y -- ) GW * + PIC + C! ;

: SHOW-CELL ( x y -- )
   2DUP CELL@ COLOUR >R
   CELL * CANVAS-Y +  SWAP CELL * CANVAS-X +  SWAP
   CELL CELL R> BOX ;

: SHOW-ALL  GH 0 DO GW 0 DO I J SHOW-CELL LOOP LOOP ;

: BAR
   CANVAS-X  CANVAS-Y BAR-Y +  CANVAS-W 16  0 COLOUR BOX
   8 0 DO
      CANVAS-X I 16 * +  CANVAS-Y BAR-Y + 2 +  14 11  I COLOUR BOX
   LOOP
   CANVAS-X INKC @ 16 * +  CANVAS-Y BAR-Y + 14 +  14 2  WHITE BOX ;

: SAY ( c-addr u -- )               \ a word on the bar
   CANVAS-X 136 +  CANVAS-Y BAR-Y +  120 16  0 COLOUR BOX
   CANVAS-X 136 +  CANVAS-Y BAR-Y +  AMBER DRAW-TEXT ;

: SAVE-PIC
   PIC AREA S" SKETCH.PIC" SAVE-FILE
   IF S" not saved" ELSE S" saved" THEN SAY ;

: LOAD-PIC ( -- flag )   PIC AREA S" SKETCH.PIC" LOAD-FILE NIP 0= ;

: CLEAR-PIC  PIC AREA 0 FILL ;

: START
   1 INKC !
   LOAD-PIC DUP 0= IF CLEAR-PIC THEN
   SHOW-ALL BAR
   IF S" loaded" ELSE S" a new picture" THEN SAY ;

: IN-GRID? ( -- flag )
   MOUSE-X CANVAS-X -  GW CELL * U<
   MOUSE-Y CANVAS-Y -  BAR-Y U<  AND ;

: ON-BAR? ( -- flag )
   MOUSE-Y CANVAS-Y - BAR-Y -  16 U<
   MOUSE-X CANVAS-X -  128 U<  AND ;

: BRUSH ( n -- )
   MOUSE-X CANVAS-X - CELL /  MOUSE-Y CANVAS-Y - CELL /
   2DUP >R >R CELL!  R> R> SHOW-CELL ;

: INK+ ( n -- ) INKC @ + 8 + 8 MOD INKC ! BAR ;

: ROW ( n -- )
   DROP HIDE-CURSOR
   MOUSE-B 32768 AND IF              \ the left button
      IN-GRID? IF INKC @ BRUSH THEN
      ON-BAR? IF MOUSE-X CANVAS-X - 16 / INKC ! BAR THEN
   THEN
   MOUSE-B 16384 AND IF              \ the right one
      IN-GRID? IF 0 BRUSH THEN
   THEN
   PRESSED
   DUP 2 AND IF -1 INK+ THEN         \ C-left
   DUP 1 AND IF 1 INK+ THEN          \ C-right
   DUP 8192 AND IF SAVE-PIC THEN     \ Z
   DUP 16 AND IF                     \ R
      LOAD-PIC IF SHOW-ALL S" loaded" ELSE S" nothing saved" THEN SAY
   THEN
   32 AND IF CLEAR-PIC SHOW-ALL S" cleared" SAY THEN
   CURSOR ;

: NEXT ;                            \ and round again
