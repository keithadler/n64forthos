\ desk.fth -- the window manager as a shell.
\
\ A prompt in a window, and whatever you OPEN in windows beside
\ it, all running at once: OPEN LIFE.FTH puts Life on the desk
\ and the prompt carries on.  EDIT opens the editor in a window of
\ its own, and ^R in it runs the file at the prompt.  Anything
\ else the prompt can do it does here -- DIR, RUN, your own words
\ -- and FILES brings up the Files window.  Keys go to the front
\ one of the prompt and the editor; ^O swaps them, and so does a
\ click.  BYE (or B) leaves the desk.
\
\ Every program gets a row of its picture each frame, and a line
\ typed at the prompt runs there and then while the others wait:
\ one processor, taking turns, the way small machines always did.
\
\ An app's words are its own business.  Once OPEN has what it
\ needs from a file -- its ROW, ROWS, START and NEXT -- it hides
\ the rest from the prompt, so a Mandelbrot's DEPTH does not
\ stand in front of the Forth one.  The desk hides its own words
\ the same way, and shows the prompt only OPEN.
\
\ To make this the machine's shell, put  RUN DESK.FTH  in a
\ BOOT.FTH of your own.

LATEST@ CONSTANT BEFORE-DESK         \ all the prompt should see

INCLUDE WM.FTH

\ --- apps, captured the moment they are compiled ---------------------------
VARIABLE MARK                        \ HERE before the file came in
: NEWEST ( c-addr u -- xt|0 )        \ only if that file made it
   FIND-NAME DUP MARK @ U< IF DROP 0 THEN ;

\ A task is seven cells: ROW, ROWS, START, NEXT, the row it is on,
\ the repaint it last drew after, and where its window was then.
: >ROW# ( task -- a ) 4 CELLS + ;
: >SEEN ( task -- a ) 5 CELLS + ;
: >WHERE ( task -- a ) 6 CELLS + ;

: NEW-TASK ( -- task | 0 )           \ after INCLUDED: 0 if not an app
   S" ROW" NEWEST DUP 0= IF EXIT THEN
   HERE SWAP ,
   S" ROWS" NEWEST DUP IF EXECUTE THEN ,
   S" START" NEWEST ,
   S" NEXT" NEWEST ,
   0 ,  -1 ,  -1 , ;

: START-TASK ( task -- ) 2 CELLS + @ ?DUP IF EXECUTE THEN ;

\ One step of one task, into the window the manager has lent it.
: TASK-STEP ( task -- )
   >R
   REPAINTS @ R@ >SEEN @ <>
   CANVAS-X 16 LSHIFT CANVAS-Y + R@ >WHERE @ <>  OR IF
      \ The desk was cleared, or the window moved: start the picture
      \ again from the top, on a clean canvas.
      REPAINTS @ R@ >SEEN !
      CANVAS-X 16 LSHIFT CANVAS-Y + R@ >WHERE !
      0 R@ >ROW# !
      CANVAS-X CANVAS-Y CANVAS-W CANVAS-H 8 10 30 RGB BOX
   THEN
   R@ >ROW# @ R@ CELL+ @ < IF          \ rows to go: draw the next
      R@ >ROW# @ R@ @ EXECUTE
      1 R@ >ROW# +!
   ELSE
      R@ 3 CELLS + @ ?DUP IF           \ an animation: on to the next pass
         DEPTH >R EXECUTE              \ NEXT may leave a flag: not wanted
         DEPTH R> > IF DROP THEN
         0 R@ >ROW# !
      THEN
   THEN
   R> DROP ;

: DRAW-TASK  THIS @ TASK-STEP ;       \ every app window draws with this

\ --- opening a file into a window ------------------------------------------
: KEEP ( c-addr u -- c-addr' u )     \ a copy of a string, in the dictionary
   TUCK HERE SWAP CMOVE  HERE SWAP DUP ALLOT ;

VARIABLE OX  VARIABLE OY  VARIABLE ONAME  VARIABLE OLEN  VARIABLE SEEN-TO

VARIABLE SPOT                        \ how many windows OPEN has placed
: NEXT-SPOT ( -- x y )               \ where the next one would go
   SPOT @ 3 MOD 208 * 16 +
   SPOT @ 3 / 2 MOD 16 * 48 + ;

: OPEN-AT ( x y c-addr u -- )        \ include it; an app gets a window
   #WINS @ MAX-WINS < 0= IF 2DROP 2DROP ." no room for another window" CR EXIT THEN
   KEEP OLEN ! ONAME !  OY ! OX !
   LATEST@ SEEN-TO !
   CANVAS-X CANVAS-Y CANVAS-W CANVAS-H
   0 0 192 192 SET-CANVAS            \ apps size themselves to this
   HERE MARK !
   ONAME @ OLEN @ INCLUDED
   >R SET-CANVAS R>
   0= IF EXIT THEN                   \ INCLUDED said what went wrong
   NEW-TASK
   ?DUP 0= IF EXIT THEN              \ not an app: it has run, and what it
                                     \ defined is the prompt's to use
   SEEN-TO @ LATEST!                 \ an app's words are its own business
   DUP START-TASK
   OX @ OY @ 196 210 ONAME @ OLEN @ ' DRAW-TASK NEW-WINDOW
   #WINS @ 1- >DATA !
   1 SPOT +!                         \ the next goes somewhere else
   1 DIRTY ! ;

\ --- who has the keys --------------------------------------------------------
\ The front one of the prompt and the editor.  The apps have no use for
\ keys, so they never get them, whatever is in front.
VARIABLE PROMPT-XT  VARIABLE EDITOR-XT
: TAKES-KEYS? ( xt -- flag ) DUP PROMPT-XT @ =  SWAP EDITOR-XT @ =  OR ;

: FRONT-TAKER ( -- xt | 0 )
   #WINS @ BEGIN DUP 0> WHILE
      1- DUP >DRAW @ DUP TAKES-KEYS? IF NIP EXIT THEN DROP
   REPEAT DROP 0 ;

: WINDOW-OF ( xt -- i | -1 )          \ the window this draw word paints
   #WINS @ 0 ?DO DUP I >DRAW @ = IF DROP I UNLOOP EXIT THEN LOOP
   DROP -1 ;

: FOCUSED? ( xt -- flag ) FRONT-TAKER = ;

\ What happens between frames, not in the middle of painting them.
VARIABLE WANT-SWITCH  VARIABLE WANT-CLOSE  VARIABLE WANT-RUN

: SWITCH                               \ bring the other key-taker forward
   FRONT-TAKER PROMPT-XT @ = IF EDITOR-XT @ ELSE PROMPT-XT @ THEN
   WINDOW-OF DUP 0< IF DROP EXIT THEN
   RAISE 1 DIRTY ! ;

\ --- the prompt ------------------------------------------------------------
: DRAW-PROMPT
   REPAINTS @  PROMPT-XT @ FOCUSED?  CONSOLE
   DUP 1 AND IF 1 DIRTY ! THEN          \ something had the screen: repaint
   2 AND IF 1 WANT-SWITCH ! THEN
   WANT-RUN @ IF                        \ the editor's ^R, run here
      0 WANT-RUN !
      NEXT-SPOT EDITED OPEN-AT
   THEN ;
' DRAW-PROMPT PROMPT-XT !

: NEW-PROMPT ( x y w h -- )  S" prompt" ' DRAW-PROMPT NEW-WINDOW ;

\ --- the editor, in a window ------------------------------------------------
: DRAW-EDITOR
   REPAINTS @  EDITOR-XT @ FOCUSED?  EDITOR
   DUP 1 AND IF 1 WANT-CLOSE ! THEN
   DUP 2 AND IF 1 WANT-SWITCH ! THEN
   4 AND IF 1 WANT-RUN ! THEN ;
' DRAW-EDITOR EDITOR-XT !

VARIABLE ET  VARIABLE ETL             \ the editor window's title

: EDIT ( "name" -- )                  \ the editor, in its window, in front
   PARSE-NAME 2DUP EDIT-OPEN ?DUP IF .IOR 2DROP EXIT THEN
   KEEP ETL ! ET !
   EDITOR-XT @ WINDOW-OF DUP 0< IF
      DROP
      #WINS @ MAX-WINS < IF
         16 48 608 224 ET @ ETL @ EDITOR-XT @ NEW-WINDOW
      THEN
   ELSE
      DUP >R  ET @ R@ >TITLE !  ETL @ R@ >TLEN !  R> RAISE
   THEN
   1 DIRTY ! ;

: BETWEEN-FRAMES                       \ what the draw words asked for
   WANT-CLOSE @ IF
      0 WANT-CLOSE !
      EDITOR-XT @ WINDOW-OF DUP 0< IF DROP ELSE CLOSE 1 DIRTY ! THEN
   THEN
   WANT-SWITCH @ IF 0 WANT-SWITCH ! SWITCH THEN ;

: DESK-FRAME  BETWEEN-FRAMES FRAME ;

: DESK-RESET
   0 #WINS !  -1 DRAGGING !  0 WAS-DOWN !  1 DIRTY !  0 SPOT !
   0 WANT-SWITCH !  0 WANT-CLOSE !  0 WANT-RUN ! ;

: DESK-START
   DESK-RESET
   16 272 608 176 NEW-PROMPT ;

\ --- what the prompt may see -----------------------------------------------
' DESK-FRAME  ' DESK-START  ' OPEN-AT  ' NEXT-SPOT  ' DESK-RESET
' NEW-PROMPT  ' EDIT
BEFORE-DESK LATEST!
CONSTANT EDIT-XT
CONSTANT NEW-PROMPT-XT  CONSTANT DESK-RESET-XT  CONSTANT NEXT-SPOT-XT
CONSTANT OPEN-AT-XT  CONSTANT DESK-START-XT  CONSTANT DESK-FRAME-XT

: OPEN ( "name" -- )  NEXT-SPOT-XT EXECUTE  PARSE-NAME  OPEN-AT-XT EXECUTE ;
: EDIT ( "name" -- )  EDIT-XT EXECUTE ;
: START  DESK-START-XT EXECUTE ;
: FRAME  DESK-FRAME-XT EXECUTE ;
