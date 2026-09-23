\ tasks.fth -- three programs at once, each in its own window,
\ and a prompt in a fourth.
\
\ Mandelbrot, Life and Navier-Stokes are the same files the
\ desktop opens one at a time.  Here the desk (DESK.FTH) opens
\ all three, and each frame asks every one of them for its next
\ row, into its own window, so they run side by side -- while
\ you type at the prompt underneath.  A line you enter runs
\ there and then; the others wait for it, as they would on any
\ machine with one processor and no preemption.
\
\ Two apps that both define ROW can share one dictionary because
\ Forth binds a call when it compiles it: the moment a file is
\ in, its ROW is the newest ROW, and that is the one the desk
\ keeps.  BYE (or B) goes back to the desktop.

INCLUDE DESK.FTH

: START
   DESK-RESET-XT EXECUTE
    16 48 S" MANDEL.FTH" OPEN-AT-XT EXECUTE
   224 48 S" LIFE.FTH"   OPEN-AT-XT EXECUTE
   432 48 S" NAVIER.FTH" OPEN-AT-XT EXECUTE
    16 272 608 176 NEW-PROMPT-XT EXECUTE ;
