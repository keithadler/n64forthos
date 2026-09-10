\ mandel.fth -- the Mandelbrot set, in 16.16 fixed point.
\
\ F* multiplies two fixed-point numbers; CANVAS-X, CANVAS-Y,
\ CANVAS-W and CANVAS-H are the rectangle the desktop lends
\ an app to draw in.  Everything below is ordinary Forth.

 65536 CONSTANT ONE           \ 1.0
262144 CONSTANT FOUR          \ 4.0, the escape radius squared
    32 CONSTANT DEPTH         \ iterations before we call it black
   640 CONSTANT STEP          \ 2.5 across 256 pixels

VARIABLE ZX  VARIABLE ZY      \ z, squared and added to death
VARIABLE MX  VARIABLE MY      \ c, the point under the pixel
VARIABLE IT

: ESCAPE ( -- n )
   0 ZX ! 0 ZY ! 0 IT !
   BEGIN
      ZX @ ZX @ F*  ZY @ ZY @ F*        ( zx2 zy2 )
      2DUP + FOUR <  IT @ DEPTH < AND
   WHILE
      ZX @ ZY @ F* 2*  MY @ +           ( zx2 zy2 zy' )
      >R  -  MX @ +  ZX !  R>  ZY !
      1 IT +!
   REPEAT
   2DROP IT @ ;

: SHADE ( n -- colour )
   DUP DEPTH < 0= IF DROP 8 10 30 RGB EXIT THEN
   DUP 9 * 255 MIN                      ( n r )
   OVER 5 * 40 + 255 MIN                ( n r g )
   ROT 3 * 90 + 255 MIN                 ( r g b )
   RGB ;

: MANDEL
   CANVAS-X CANVAS-Y CANVAS-W CANVAS-H  8 10 30 RGB  BOX
   CANVAS-H 0 DO
      -81920 I STEP * + MY !            \ -1.25 down the rows
      CANVAS-W 0 DO
         -131072 I STEP * + MX !        \ -2.0 across the columns
         ESCAPE SHADE
         CANVAS-X I +  CANVAS-Y J +  ROT
         PLOT
      LOOP
   LOOP ;
