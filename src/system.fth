\ ===========================================================================
\ system.fth -- n64forthos, the part of the system written in Forth
\
\ Interpreted at boot, straight out of the ROM, before anything is drawn to
\ the console.  Definitions must appear before their first use: the kernel's
\ dictionary is a linear search from the newest word backwards.
\
\ Comments and blank lines are stripped by tools/mkboot.py, so they cost
\ nothing in the image.
\ ===========================================================================

\ --- words the kernel leaves to Forth --------------------------------------
: ? @ . ;
: NOT 0= ;
: SQUARED DUP * ;
: STAR 42 EMIT ;
: STARS 0 DO STAR LOOP ;
: BL 32 ;
: TRUE -1 ;
: FALSE 0 ;

\ --- the palette -----------------------------------------------------------
\ RGB takes r g b on the stack and leaves a 5551 pixel.
 96 224 255 RGB CONSTANT CYAN
255 190  90 RGB CONSTANT AMBER
235 240 250 RGB CONSTANT WHITE
110 125 155 RGB CONSTANT GREY
 10  14  30 RGB CONSTANT INKY
 20  26  56 RGB CONSTANT SLATE

640 CONSTANT SCREEN-W
480 CONSTANT SCREEN-H

\ --- the greeting ----------------------------------------------------------
\ The first thing this system was ever asked to say.
: HELLO ." Hello, World!" CR ;

\ --- drawing ---------------------------------------------------------------
\ The kernel's drawing words are the machine's graphics API: BOX FRAME LINE
\ HLINE VLINE PLOT DRAW-TEXT BLIT-SPRITE, all of them taking pixels on the
\ stack.  Everything below is written in terms of those.

\ Eight colour bars: the shortest proof that Forth can reach the pixels.
: BARS
   8 0 DO
      I 74 * 24 +  420  64 40
      I 26 * 40 + 200 255 RGB
      BOX
   LOOP ;

\ A window: filled panel, border, title bar.  The desktop starts here.
VARIABLE PX  VARIABLE PY  VARIABLE PW  VARIABLE PH
: PANEL ( x y w h -- )
   PH ! PW ! PY ! PX !
   PX @ PY @ PW @ PH @ SLATE BOX
   PX @ PY @ PW @ 18 CYAN BOX
   PX @ PY @ PW @ PH @ CYAN FRAME ;

: HELLO-WINDOW
   408 192 216 120 PANEL
   S" Hello, World!"        416 192 INKY  DRAW-TEXT
   S" a window drawn"       416 224 WHITE DRAW-TEXT
   S" in Forth words"       416 240 GREY  DRAW-TEXT
   416 272 616 272 CYAN LINE ;

: GREET
   CYAN INK HELLO
   GREY INK ." Forth is up. 640x480, sixteen bits a pixel." CR
   WHITE INK ;
