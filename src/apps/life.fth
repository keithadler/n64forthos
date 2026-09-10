\ life.fth -- Conway's life, 64 by 64, on a torus.
\
\ Two buffers in the dictionary; the desktop asks for one row of
\ the next generation per frame and NEXT swaps them over, so it
\ runs until you stop it.

64 CONSTANT SIDE
SIDE SIDE * CONSTANT AREA
SIDE CONSTANT ROWS
CANVAS-W SIDE / CONSTANT SCALE         \ four pixels to a cell

\ A VARIABLE hands back the address of its body, and ALLOT makes
\ that body as big as we like: two generations of cells.
VARIABLE BUF-A  AREA ALLOT
VARIABLE BUF-B  AREA ALLOT

VARIABLE GRID                          \ the generation being read
VARIABLE NEXTG                         \ the one being written
VARIABLE CX  VARIABLE CY
VARIABLE SEED

 96 224 255 RGB CONSTANT ALIVE
  8  10  30 RGB CONSTANT DEAD

: RND ( -- n )                         \ any old linear congruence
   SEED @ 1103515245 * 12345 + DUP SEED !
   16 RSHIFT 32767 AND ;

: START
   12345 SEED !
   BUF-A GRID !  BUF-B NEXTG !
   AREA 0 DO
      RND 3 AND 0= IF 1 ELSE 0 THEN
      GRID @ I + C!
   LOOP ;

: WRAP ( n -- n ) SIDE + SIDE MOD ;    \ the edges meet

: LIVE? ( x y -- 0|1 )
   WRAP SIDE * SWAP WRAP + GRID @ + C@ ;

: N# ( dx dy -- 0|1 )
   CY @ + SWAP CX @ + SWAP LIVE? ;

: NEIGHBOURS ( -- n )
   -1 -1 N#   0 -1 N# +   1 -1 N# +
   -1  0 N# +             1  0 N# +
   -1  1 N# +   0  1 N# +   1  1 N# + ;

: NEXT-CELL ( -- 0|1 )                 \ three to be born, two to live on
   NEIGHBOURS DUP 3 = IF DROP 1 EXIT THEN
   2 = CX @ CY @ LIVE? AND ;

: ROW ( n -- )
   DUP CY !
   SCALE * CANVAS-Y +                  \ the pixel row it lands on
   SIDE 0 DO
      I CX !
      NEXT-CELL
      DUP  CY @ SIDE * CX @ +  NEXTG @ +  C!
      IF ALIVE ELSE DEAD THEN
      OVER
      CANVAS-X I SCALE * +  SWAP
      SCALE SCALE  4 ROLL
      BOX
   LOOP
   DROP ;

: NEXT                                 \ swap the buffers and go again
   GRID @ NEXTG @  GRID !  NEXTG !
   -1 ;
