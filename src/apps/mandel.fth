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

\ One sample per 2x2 block, and one row of blocks per call: the
\ desktop asks for row n once a frame, so the machine keeps
\ reading its controller while the picture paints.
CANVAS-W 2/ CONSTANT RW
CANVAS-H 2/ CONSTANT RH
RH CONSTANT ROWS

: ROW ( n -- )
   DUP -81920 SWAP STEP 2* * + MY !     \ -1.25 down the rows
   2* CANVAS-Y +                        \ y
   RW 0 DO
      -131072 I STEP 2* * + MX !        \ -2.0 across the columns
      ESCAPE SHADE                      \ y colour
      OVER
      CANVAS-X I 2* +  SWAP             \ y c x y
      2 2  4 ROLL                       \ y x y w h c
      BOX
   LOOP
   DROP ;
