\ navier.fth -- the finite-time blowup, in the paper's own
\ similarity variables, 16.16 fixed point.
\
\ A slice through the vortex: radius across, height up.  The
\ colour is the speed.  Each pass drops tau by a fifth, so the
\ core tightens and the speeds grow while you watch -- which is
\ the whole point of the construction.
\
\   core radius   rc = sqrt tau           two square roots give
\   axial scale   zc = tau^1/4            the quarter power
\   speed         |u| ~ tau^-3/4
\
\ The profile functions are solved numerically in the paper, so
\ these are closed-form stand-ins with the same behaviour on the
\ axis and far away:  g = p over 1+p*p squared, s = 1 over 1+z*z.

CANVAS-W 2/ CONSTANT RW                \ one sample per 2x2 block
CANVAS-H 2/ CONSTANT RH
RH CONSTANT ROWS

 65536 CONSTANT ONE
131072 RH / CONSTANT ZSTEP             \ height spans -1 .. 1
 65536 RW / CONSTANT RSTEP             \ radius spans  0 .. 1
 52429 CONSTANT FIFTH                  \ 0.8
  1311 CONSTANT TAU-MIN                \ 0.02, then start again
 32768 CONSTANT TAU-MAX                \ 0.5

VARIABLE TAU
VARIABLE RC    VARIABLE INVRC
VARIABLE ZC    VARIABLE AMP
VARIABLE SZ                            \ s zeta, for the row in hand
VARIABLE PEAK                          \ the largest speed at this tau

16711680 CONSTANT FULL                 \ 255.0 in 16.16
23396352 CONSTANT HOT                  \ 357.0: red saturates early

: CLAMP ( n -- n ) 0 MAX 255 MIN ;

\ Black to red to orange to white, smoothly: red rises with the
\ speed, green with its square, blue with the fourth power.
: HEAT ( u -- colour )
   DUP HOT F* CLAMP                    \ u r
   OVER DUP F* FULL F* CLAMP           \ u r g
   ROT DUP F* DUP F* FULL F* CLAMP     \ r g b
   RGB ;

: SCALES                               \ the similarity scalings
   TAU @ FSQRT RC !
   ONE RC @ F/ INVRC !
   RC @ FSQRT ZC !                     \ tau^1/4
   ONE  RC @ ZC @ F*  F/ AMP !         \ speed grows as tau^-3/4
   AMP @ 21299 F* PEAK ! ;             \ g peaks at 0.325, so scale by it

: START  TAU-MAX TAU ! SCALES ;

: NEXT                                 \ the desktop calls this each pass
   TAU @ FIFTH F* TAU !
   TAU @ TAU-MIN < IF TAU-MAX TAU ! THEN
   SCALES  -1 ;                        \ true, so the desktop loops

: LABELS
   S" Navier-Stokes: the core tightens"
      CANVAS-X 8 +  CANVAS-Y CANVAS-H + 40 -  235 240 250 RGB DRAW-TEXT
   S" radius ~ sqrt(tau)   speed ~ tau^-3/4"
      CANVAS-X 8 +  CANVAS-Y CANVAS-H + 24 -  110 125 155 RGB DRAW-TEXT
   \ where the core is now, and the axis it is collapsing onto
   CANVAS-X  RC @ RSTEP /  2*  +
      CANVAS-Y  CANVAS-H 24 -  96 224 255 RGB VLINE
   CANVAS-X CANVAS-Y CANVAS-H 24 - 255 255 255 RGB VLINE
   \ and how fast the fluid is going now, as a bar
   CANVAS-X 8 +  CANVAS-Y CANVAS-H + 12 -
      AMP @ 24 * 65536 /  6  255 190 90 RGB  BOX ;

: ROW ( n -- )
   DUP 0= IF LABELS THEN
   DUP RH 2/ -  ZSTEP *  ZC @ F/       \ n zeta
   DUP F* ONE +  ONE SWAP F/  SZ !     \ s = 1 over 1 plus zeta squared
   CANVAS-Y SWAP 2* +                  \ y
   RW 0 DO
      I RSTEP *  INVRC @ F*            \ y p
      DUP  DUP F* ONE +                \ y p 1+pp
      DUP F*                           \ y p 1+pp squared
      F/                               \ y g
      SZ @ F*  21299 F/                \ y, the profile against its own peak
      DUP F* DUP F* DUP F*             \ to the eighth: a ring, not a blob
      HEAT                             \ y colour
      OVER
      CANVAS-X I 2* +  SWAP            \ y c x y
      2 2  4 ROLL                      \ y x y w h c
      BOX
   LOOP
   DROP ;
