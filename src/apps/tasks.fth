\ tasks.fth -- three programs at once, each in its own window.
\
\ Mandelbrot, Life and Navier-Stokes are the same files the
\ desktop opens one at a time.  Here the window manager includes
\ all three, and each frame asks every one of them for its next
\ row, into its own window, so they run side by side.
\
\ Two apps that both define ROW can live in one dictionary
\ because Forth binds a call when it compiles it: the moment a
\ file has been included, its ROW is the newest ROW, and that is
\ the one captured.  Whatever is defined after cannot change it.

INCLUDE WM.FTH

\ --- looking up what the file just defined ---------------------------------
VARIABLE MARK                         \ HERE before the file came in
: NEWEST ( c-addr u -- xt|0 )         \ only if the newest file made it
   FIND-NAME DUP MARK @ U< IF DROP 0 THEN ;

\ --- a task: an app's words, and how far it has got ------------------------
\ Seven cells: ROW, ROWS, START, NEXT, the row it is on, the
\ repaint it last drew after, and where its window was then.
: >ROW# ( task -- a ) 4 CELLS + ;
: >SEEN ( task -- a ) 5 CELLS + ;
: >WHERE ( task -- a ) 6 CELLS + ;

: TASK ( "name" -- )                  \ after INCLUDE: capture the app
   CREATE
   S" ROW" NEWEST ,
   S" ROWS" NEWEST DUP IF EXECUTE THEN ,
   S" START" NEWEST ,
   S" NEXT" NEWEST ,
   0 ,  -1 ,  -1 , ;

\ One step of one task, into the window the manager has lent it.  (The
\ names here are ones no app uses: a word compiled after the apps come in
\ would otherwise find theirs -- MANDEL.FTH has a STEP of its own.)
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

: START-TASK ( task -- ) 2 CELLS + @ ?DUP IF EXECUTE THEN ;

\ --- the three, each compiled for a 192 by 192 canvas ---------------------
0 0 192 192 SET-CANVAS

HERE MARK !
INCLUDE MANDEL.FTH
TASK T-MANDEL

HERE MARK !
INCLUDE LIFE.FTH
TASK T-LIFE

HERE MARK !
INCLUDE NAVIER.FTH
TASK T-NAVIER

: DRAW-T-MANDEL  T-MANDEL TASK-STEP ;
: DRAW-T-LIFE    T-LIFE TASK-STEP ;
: DRAW-T-NAVIER  T-NAVIER TASK-STEP ;

: DRAW-ABOUT
   S" Three programs, one machine: each window"
      CANVAS-X 8 + CANVAS-Y 8 + PAPER DRAW-TEXT
   S" gets a row of its picture every frame."
      CANVAS-X 8 + CANVAS-Y 24 + PAPER DRAW-TEXT
   S" Drag them about; B goes back to the desktop."
      CANVAS-X 8 + CANVAS-Y 48 + BEHIND DRAW-TEXT ;

\ --- the desk --------------------------------------------------------------
: START
   0 #WINS !  -1 DRAGGING !  0 WAS-DOWN !  1 DIRTY !
   T-MANDEL START-TASK  T-LIFE START-TASK  T-NAVIER START-TASK
    16  48 196 210  S" MANDEL.FTH"  ' DRAW-T-MANDEL  NEW-WINDOW
   224  48 196 210  S" LIFE.FTH"    ' DRAW-T-LIFE    NEW-WINDOW
   432  48 196 210  S" NAVIER.FTH"  ' DRAW-T-NAVIER  NEW-WINDOW
    16 288 384 96   S" tasks"       ' DRAW-ABOUT     NEW-WINDOW ;

\ The window manager's FRAME, by a name the desktop can see here.
' FRAME CONSTANT WM-FRAME
: FRAME  WM-FRAME EXECUTE ;
