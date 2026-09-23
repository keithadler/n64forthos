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
: -ROT ROT ROT ;
: /MOD ( n1 n2 -- rem quot ) 2DUP MOD >R / R> SWAP ;
: */ ( n1 n2 n3 -- n1*n2/n3 ) >R * R> / ;
: 0<> 0= 0= ;
: ERASE ( addr n -- ) 0 FILL ;
: WITHIN ( n lo hi -- flag ) OVER - >R - R> U< ;

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

\ --- the shell ---------------------------------------------------------------
\ The file system is in the kernel (src/fs.c); the commands you type are
\ here, in Forth, on top of LOAD-FILE, SAVE-FILE and friends.  INCLUDE, EDIT
\ and RUN are the kernel's own, because they run other source.

VARIABLE FBUF  31744 ALLOT              \ a file's worth of room

: DIGITS ( n -- d ) 1 SWAP BEGIN 10 / DUP WHILE SWAP 1+ SWAP REPEAT DROP ;
: .R ( n width -- ) OVER DIGITS - 0 MAX SPACES . ;
: .NAME ( c-addr u -- ) TUCK TYPE 20 SWAP - 1 MAX SPACES ;

: .DISK ( state -- )
   DUP 1 = IF DROP DISK-FREE . ." bytes free on the Controller Pak" CR EXIT THEN
   DUP 2 = IF DROP DISK-FREE . ." bytes free, RAM disk (no pak)" CR EXIT THEN
   DUP 3 = IF DROP ." the Controller Pak is not formatted: FORMAT" CR EXIT THEN
   DUP 4 = IF DROP ." the pak's directory is damaged: FORMAT" CR EXIT THEN
   DROP ." the Controller Pak is not answering" CR ;

: DIR
   #FILES ?DUP IF
      0 DO
         I FILE# >R >R  2 SPACES .NAME  R> 6 .R  R> .VOL CR
      LOOP
   THEN
   DISK-STATE .DISK ;
: LS DIR ;

: .DONE ( ior c-addr u -- ) ROT ?DUP IF .IOR 2DROP ELSE TYPE CR THEN ;

: TYPE-FILE ( addr n -- )               \ with a CR unless it ends in one
   DUP 0= IF 2DROP EXIT THEN
   2DUP TYPE + 1- C@ 10 <> IF CR THEN ;

: CAT
   PARSE-NAME FBUF 31744 2SWAP LOAD-FILE
   ?DUP IF .IOR DROP ELSE FBUF SWAP TYPE-FILE THEN ;

: DEL  PARSE-NAME DELETE-FILE S" deleted" .DONE ;
: REN  PARSE-NAME PARSE-NAME RENAME-FILE S" renamed" .DONE ;
: COPY
   PARSE-NAME FBUF 31744 2SWAP LOAD-FILE
   ?DUP IF .IOR DROP PARSE-NAME 2DROP EXIT THEN
   FBUF SWAP PARSE-NAME SAVE-FILE S" copied" .DONE ;

: FORMAT
   ." FORMAT erases everything on the Controller" CR
   ." Pak, game saves too. To go ahead, type:" CR
   ."    ERASE-PAK" CR ;
: ERASE-PAK  (FORMAT) S" formatted" .DONE  DISK-STATE .DISK ;

: MEM
   UNUSED . ." bytes free for Forth" CR
   DISK-STATE .DISK ;

: HELP
   CYAN INK ." Files" WHITE INK CR
   2 SPACES ." DIR  CAT name  EDIT name  RUN name" CR
   2 SPACES ." INCLUDE name  DEL name  REN old new" CR
   2 SPACES ." COPY from to  FORMAT  MEM  FILES  BYE" CR
   CYAN INK ." Forth" WHITE INK CR
   2 SPACES ." WORDS  SEE name  FORGET name  .S" CR
   2 SPACES ." Esc, ^C or START+Z stops a program" CR
   2 SPACES ." CAT README.TXT tells you the rest" CR ;
