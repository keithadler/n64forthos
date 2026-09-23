\ wm.fth -- a window manager, written in Forth.
\
\ The kernel draws rectangles, text and a pointer, and clips what
\ it is told to clip.  Everything above that -- what a window is,
\ which one is in front, what happens when you drag a title bar --
\ is here, in the language the machine speaks.
\
\ A window is seven cells: x, y, width, height, the address and
\ length of its title, and the word that draws its contents.  That
\ word runs with the canvas set to the window's inside and the clip
\ set to match, so it cannot draw anywhere else even if it tries.

   6 CONSTANT MAX-WINS
   7 CONSTANT FIELDS
  16 CONSTANT BAR                      \ a title bar is one row of text
  14 CONSTANT SHUT                     \ width of the close box

VARIABLE WINS   MAX-WINS FIELDS * CELLS ALLOT
VARIABLE #WINS
VARIABLE DRAGGING                      \ which window, or -1
VARIABLE GRABX  VARIABLE GRABY
VARIABLE WAS-DOWN

 20  26  56 RGB CONSTANT PANEL
 96 224 255 RGB CONSTANT FRONT
 70  86 130 RGB CONSTANT BEHIND
 10  14  30 RGB CONSTANT INK
205 213 228 RGB CONSTANT PAPER
 16  20  44 RGB CONSTANT DESK
255 120 120 RGB CONSTANT SHUT-COLOUR

\ --- the record ------------------------------------------------------------
: WIN ( i -- a )    FIELDS * CELLS WINS + ;
: >X ( i -- a )     WIN ;
: >Y ( i -- a )     WIN CELL+ ;
: >W ( i -- a )     WIN 2 CELLS + ;
: >H ( i -- a )     WIN 3 CELLS + ;
: >TITLE ( i -- a ) WIN 4 CELLS + ;
: >TLEN ( i -- a )  WIN 5 CELLS + ;
: >DRAW ( i -- a )  WIN 6 CELLS + ;

VARIABLE WX  VARIABLE WY  VARIABLE WW  VARIABLE WH
: LOAD ( i -- i )
   DUP >X @ WX !   DUP >Y @ WY !
   DUP >W @ WW !   DUP >H @ WH ! ;

VARIABLE CX  VARIABLE CY  VARIABLE CW  VARIABLE CH
: CLIENT                               \ the inside, from the loaded window
   WX @ 2 + CX !
   WY @ BAR + CY !
   WW @ 4 - CW !
   WH @ BAR - 2 - CH ! ;

\ --- making and unmaking ---------------------------------------------------
VARIABLE NX VARIABLE NY VARIABLE NW VARIABLE NH
VARIABLE NT VARIABLE NL VARIABLE ND

: NEW-WINDOW ( x y w h addr len xt -- )
   ND ! NL ! NT ! NH ! NW ! NY ! NX !
   #WINS @ MAX-WINS < IF
      #WINS @
      NX @ OVER >X !   NY @ OVER >Y !
      NW @ OVER >W !   NH @ OVER >H !
      NT @ OVER >TITLE !  NL @ OVER >TLEN !
      ND @ OVER >DRAW !
      DROP
      1 #WINS +!
   THEN ;

VARIABLE TMP   FIELDS CELLS ALLOT

: COPY-WIN ( from to -- )              \ seven cells, either way
   FIELDS 0 DO
      OVER I CELLS + @
      OVER I CELLS + !
   LOOP 2DROP ;

: RAISE ( i -- )                       \ painting is in order, so move it last
   DUP #WINS @ 1- < IF
      DUP WIN TMP COPY-WIN
      BEGIN DUP #WINS @ 1- < WHILE
         DUP 1+ WIN  OVER WIN  COPY-WIN
         1+
      REPEAT
      WIN TMP SWAP COPY-WIN
   ELSE DROP THEN ;

: CLOSE ( i -- )
   BEGIN DUP #WINS @ 1- < WHILE
      DUP 1+ WIN  OVER WIN  COPY-WIN
      1+
   REPEAT
   DROP
   #WINS @ 1- 0 MAX #WINS ! ;

\ --- what is under the pointer ---------------------------------------------
: IN-WINDOW? ( i -- flag )             \ is the pointer inside window i?
   LOAD DROP
   MOUSE-X WX @ -  WW @ U<
   MOUSE-Y WY @ -  WH @ U<  AND ;

: TOPMOST ( -- i )                     \ front to back, -1 for none
   #WINS @ 0 DO
      #WINS @ 1- I -
      DUP IN-WINDOW? IF UNLOOP EXIT THEN
      DROP
   LOOP
   -1 ;

\ --- painting --------------------------------------------------------------
: PAINT ( i -- )
   LOAD
   WX @ WY @ WW @ WH @ PANEL BOX
   WX @ WY @ WW @ BAR
      4 PICK #WINS @ 1- = IF FRONT ELSE BEHIND THEN
      BOX
   DUP >TITLE @ OVER >TLEN @
      WX @ 8 + WY @ INK DRAW-TEXT
   WX @ WW @ + SHUT -  WY @ 2 +  10 12  SHUT-COLOUR BOX
   WX @ WY @ WW @ WH @ BEHIND FRAME
   CLIENT
   CX @ CY @ CW @ CH @ CLIP
   CX @ CY @ CW @ CH @ SET-CANVAS
   >DRAW @ EXECUTE
   NOCLIP ;

VARIABLE DIRTY                         \ has the layout changed?
VARIABLE REPAINTS                      \ how many times the desk was cleared

: CONTENTS ( i -- )                    \ the inside of one window, no chrome
   LOAD CLIENT
   CX @ CY @ CW @ CH @ CLIP
   CX @ CY @ CW @ CH @ SET-CANVAS
   >DRAW @ EXECUTE
   NOCLIP ;

\ Repainting the whole screen every frame would flicker: you would see the
\ desk cleared before the windows came back.  The chrome is drawn when
\ something moves, and between times only the contents are, each inside its
\ own window where nothing else can see it.
: REPAINT
   1 REPAINTS +!                       \ what a window drew is gone now
   0 16 SCREEN-W SCREEN-H 16 - DESK BOX
   S" drag a title bar   red box closes   B leaves"
      16 SCREEN-H 32 - BEHIND DRAW-TEXT
   #WINS @ 0 DO I PAINT LOOP
   0 DIRTY ! ;

: REDRAW-CONTENTS
   #WINS @ 0 DO I CONTENTS LOOP ;

\ --- the mouse -------------------------------------------------------------
VARIABLE HIT

: PRESS                                \ a button has just gone down
   TOPMOST DUP 0< IF DROP EXIT THEN
   RAISE
   #WINS @ 1- LOAD DROP
   MOUSE-Y WY @ -  BAR U< IF           \ in the title bar
      MOUSE-X  WX @ WW @ + SHUT -  -  10 U< IF
         #WINS @ 1- CLOSE
      ELSE
         #WINS @ 1- DRAGGING !
         MOUSE-X WX @ - GRABX !
         MOUSE-Y WY @ - GRABY !
      THEN
   THEN
   1 DIRTY ! ;

: DRAG                                 \ carry the window with the pointer
   DRAGGING @ 0< IF EXIT THEN
   DRAGGING @ LOAD
   MOUSE-X GRABX @ -  0 MAX  SCREEN-W WW @ - MIN  OVER >X !
   MOUSE-Y GRABY @ -  16 MAX  SCREEN-H WH @ - MIN  SWAP >Y !
   1 DIRTY ! ;

: HANDLE
   MOUSE-B 32768 AND HIT !
   HIT @ WAS-DOWN @ 0= AND IF PRESS THEN
   HIT @ IF DRAG ELSE -1 DRAGGING ! THEN
   HIT @ WAS-DOWN ! ;

\ --- the frame the desktop asks for ----------------------------------------
: FRAME
   POLL
   HIDE-CURSOR
   HANDLE
   DIRTY @ IF REPAINT ELSE REDRAW-CONTENTS THEN
   CURSOR ;

\ --- three windows to prove it ---------------------------------------------
: DRAW-HELLO
   S" Hello, World!"
      CANVAS-X 8 + CANVAS-Y 10 + PAPER DRAW-TEXT
   S" a window, and the window"
      CANVAS-X 8 + CANVAS-Y 34 + BEHIND DRAW-TEXT
   S" manager, both in Forth"
      CANVAS-X 8 + CANVAS-Y 50 + BEHIND DRAW-TEXT ;

: DRAW-BARS
   8 0 DO
      CANVAS-X I 22 * + 6 +   CANVAS-Y 6 +   18   CANVAS-H 12 -
      I 26 * 40 + 200 255 RGB
      BOX
   LOOP ;

: DRAW-BOUNCE
   CANVAS-X CANVAS-Y CANVAS-W CANVAS-H INK BOX
   TICKS 2/  CANVAS-W 36 - 2* MOD  DUP  CANVAS-W 36 - > IF
      CANVAS-W 36 - 2* SWAP -
   THEN
   CANVAS-X + 2 +
   CANVAS-Y CANVAS-H 2/ + 14 -   32 28
   255 190 90 RGB BOX ;

\ --- a window that computes -----------------------------------------------
\ Escape-time, a couple of rows a frame, straight into the window's canvas.
\ It notices when the window has been carried somewhere else and starts
\ again, because the pixels it drew stayed where they were.

262144 CONSTANT FOUR
    24 CONSTANT DEPTH
VARIABLE ZX  VARIABLE ZY
VARIABLE MX  VARIABLE MY
VARIABLE IT
VARIABLE MROW  VARIABLE LASTX  VARIABLE LASTY

: ESCAPE ( -- n )
   0 ZX ! 0 ZY ! 0 IT !
   BEGIN
      ZX @ ZX @ F*  ZY @ ZY @ F*
      2DUP + FOUR <  IT @ DEPTH < AND
   WHILE
      ZX @ ZY @ F* 2*  MY @ +
      >R  -  MX @ +  ZX !  R>  ZY !
      1 IT +!
   REPEAT
   2DROP IT @ ;

: SHADE ( n -- colour )
   DUP DEPTH < 0= IF DROP 8 10 30 RGB EXIT THEN
   DUP 11 * 255 MIN
   OVER 6 * 40 + 255 MIN
   ROT 4 * 90 + 255 MIN
   RGB ;

: MANDEL-ROW ( n -- )                  \ one row of the window's canvas
   DUP CANVAS-H < 0= IF DROP EXIT THEN
   DUP -81920 SWAP 163840 CANVAS-H / * + MY !
   CANVAS-Y +
   CANVAS-W 0 DO
      -131072 I 163840 CANVAS-W / * + MX !
      ESCAPE SHADE
      OVER
      CANVAS-X I +  SWAP  ROT
      PLOT
   LOOP
   DROP ;

: DRAW-MANDEL
   CANVAS-X LASTX @ <>  CANVAS-Y LASTY @ <>  OR IF
      0 MROW !  CANVAS-X LASTX !  CANVAS-Y LASTY !
   THEN
   2 0 DO
      MROW @ MANDEL-ROW
      MROW @ CANVAS-H < IF 1 MROW +! THEN
   LOOP ;

: START
   0 #WINS !  -1 DRAGGING !  0 WAS-DOWN !  1 DIRTY !
   \ Whole characters across and whole rows down: the titles are then on
   \ the same grid as everything else the system draws.
    40  64 264 112  S" hello"    ' DRAW-HELLO  NEW-WINDOW
   152 208 240 128  S" colours"  ' DRAW-BARS   NEW-WINDOW
   328  96 248 128  S" bounce"   ' DRAW-BOUNCE NEW-WINDOW
   -1 LASTX !  -1 LASTY !
   360 256 216 176  S" mandel"   ' DRAW-MANDEL NEW-WINDOW ;
