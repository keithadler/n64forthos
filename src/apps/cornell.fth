\ cornell.fth -- a Cornell box, ray traced, 16.16 fixed point.
\
\ Five walls (red left, green right, white floor, ceiling and
\ back), two spheres and one light, with a shadow ray.  The
\ camera sits just inside the open front and looks at the back.

VARIABLE RX VARIABLE RY VARIABLE RZ    \ ray origin
VARIABLE DX VARIABLE DY VARIABLE DZ    \ ray direction
VARIABLE TT                            \ nearest hit so far
VARIABLE NX VARIABLE NY VARIABLE NZ    \ normal there
VARIABLE CR VARIABLE CG VARIABLE CB    \ albedo there
VARIABLE HX VARIABLE HY VARIABLE HZ    \ the hit point
VARIABLE GX VARIABLE GY VARIABLE GZ    \ normal, kept for shading
VARIABLE AR VARIABLE AG VARIABLE AB    \ albedo, kept for shading
VARIABLE SX VARIABLE SY VARIABLE SZ    \ sphere centre
VARIABLE SR                            \ sphere radius
VARIABLE MR VARIABLE MG VARIABLE MB    \ sphere albedo
VARIABLE EX VARIABLE EY VARIABLE EZ    \ origin minus centre
VARIABLE QA VARIABLE QB VARIABLE QC VARIABLE QD
VARIABLE LX VARIABLE LY VARIABLE LZ    \ direction to the light
VARIABLE LD                            \ distance to it
VARIABLE LIT

 65536 CONSTANT ONE
-65536 CONSTANT -ONE

\ One ray per 2x2 block: a quarter of the work, and the chunky
\ pixels suit the machine.
CANVAS-W 2/ CONSTANT RW
CANVAS-H 2/ CONSTANT RH
117760 RW / CONSTANT FOV               \ +-0.9 across the canvas

: TRY ( t -- t flag )  DUP 0> OVER TT @ < AND ;

: WALL-X
   DX @ 0= IF EXIT THEN
   DX @ 0< IF -ONE ELSE ONE THEN  RX @ -  DX @ F/
   TRY IF TT !
      0 NY ! 0 NZ !
      DX @ 0< IF  ONE NX ! 205 CR !  55 CG !  55 CB !
              ELSE -ONE NX !  55 CR ! 205 CG !  65 CB ! THEN
   ELSE DROP THEN ;

: WALL-Y
   DY @ 0= IF EXIT THEN
   DY @ 0< IF -ONE ELSE ONE THEN  RY @ -  DY @ F/
   TRY IF TT !
      0 NX ! 0 NZ !
      DY @ 0< IF ONE NY ! ELSE -ONE NY ! THEN
      210 CR ! 210 CG ! 200 CB !
   ELSE DROP THEN ;

: WALL-Z                               \ only the back; the front is open
   DZ @ 0< 0= IF EXIT THEN
   -ONE RZ @ -  DZ @ F/
   TRY IF TT !
      0 NX ! 0 NY ! ONE NZ !
      210 CR ! 210 CG ! 200 CB !
   ELSE DROP THEN ;

: SPHERE                               \ the sphere in SX SY SZ SR
   RX @ SX @ - EX !  RY @ SY @ - EY !  RZ @ SZ @ - EZ !
   DX @ DX @ F* DY @ DY @ F* + DZ @ DZ @ F* + QA !
   EX @ DX @ F* EY @ DY @ F* + EZ @ DZ @ F* + 2* QB !
   EX @ EX @ F* EY @ EY @ F* + EZ @ EZ @ F* +
      SR @ SR @ F* - QC !
   QB @ QB @ F*  QA @ QC @ F* 4 * -  QD !
   QD @ 0> IF
      QB @ NEGATE  QD @ FSQRT -  QA @ 2* F/
      TRY IF TT !
         RX @ DX @ TT @ F* + HX !
         RY @ DY @ TT @ F* + HY !
         RZ @ DZ @ TT @ F* + HZ !
         HX @ SX @ - SR @ F/ NX !
         HY @ SY @ - SR @ F/ NY !
         HZ @ SZ @ - SR @ F/ NZ !
         MR @ CR ! MG @ CG ! MB @ CB !
      ELSE DROP THEN
   THEN ;

: LEFT-BALL   -39321 SY ! -19661 SX ! -13107 SZ ! 26214 SR !
              120 MR ! 170 MG ! 255 MB ! SPHERE ;
: RIGHT-BALL  -42598 SY ! 26214 SX ! 19661 SZ ! 22937 SR !
              255 MR ! 205 MG ! 120 MB ! SPHERE ;

: SCENE  WALL-X WALL-Y WALL-Z LEFT-BALL RIGHT-BALL ;

: RAY ( col row -- )                   \ the primary ray for a pixel
   0 RX ! 0 RY ! 55705 RZ !            \ camera at z = 0.85
   RH 2/ SWAP - FOV * DY !             \ v, up the screen
   RW 2/ - FOV * DX !                  \ u, across it
   -ONE DZ ! ;

: SEE-LIGHT                            \ direction and distance to the lamp
   0     HX @ - LX !
   55705 HY @ - LY !
   -6553 HZ @ - LZ !
   LX @ LX @ F* LY @ LY @ F* + LZ @ LZ @ F* + FSQRT LD !
   LX @ LD @ F/ LX !
   LY @ LD @ F/ LY !
   LZ @ LD @ F/ LZ ! ;

: SHADOWED ( -- flag )                 \ is anything between us and it?
   HX @ LX @ 655 F* + RX !
   HY @ LY @ 655 F* + RY !
   HZ @ LZ @ 655 F* + RZ !
   LX @ DX !  LY @ DY !  LZ @ DZ !
   LD @ TT !
   LEFT-BALL RIGHT-BALL
   TT @ LD @ < ;

: SHADE ( -- colour )                  \ Lambert, ambient, one shadow ray
   RX @ DX @ TT @ F* + HX !
   RY @ DY @ TT @ F* + HY !
   RZ @ DZ @ TT @ F* + HZ !
   NX @ GX !  NY @ GY !  NZ @ GZ !
   CR @ AR !  CG @ AG !  CB @ AB !
   SEE-LIGHT
   16384 LIT !                         \ ambient, 0.25
   SHADOWED 0= IF
      GX @ LX @ F* GY @ LY @ F* + GZ @ LZ @ F* +
      DUP 0> IF 52428 F* LIT +! ELSE DROP THEN
   THEN
   AR @ LIT @ F* 255 MIN
   AG @ LIT @ F* 255 MIN
   AB @ LIT @ F* 255 MIN
   RGB ;

: PIXEL ( col row -- colour )
   RAY  1966080 TT !  SCENE            \ 30.0 is far enough away
   SHADE ;

: CORNELL
   CANVAS-X CANVAS-Y CANVAS-W CANVAS-H  8 10 30 RGB  BOX
   RH 0 DO
      RW 0 DO
         I J PIXEL                     ( colour )
         CANVAS-X I 2* +               ( c x )
         CANVAS-Y J 2* +               ( c x y )
         2 2                           ( c x y w h )
         4 ROLL                        ( x y w h c )
         BOX
      LOOP
   LOOP ;

