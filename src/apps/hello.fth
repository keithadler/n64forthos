\ hello.fth -- a first program, and a place to start your own.
\
\ Run it: START in Files, or type  INCLUDE HELLO.FTH  at the
\ prompt.  Change it: EDIT HELLO.FTH, then ^S to save.  Your
\ copy goes on the Controller Pak and is the one that runs from
\ then on; delete it and this one comes back.

: GREETING
   CYAN INK ." Hello from a file on the Nintendo 64!" CR
   WHITE INK ;

: COUNTDOWN ( n -- )
   BEGIN DUP 0> WHILE DUP . 1- REPEAT
   DROP ." liftoff" CR ;

: FILES-HERE ( -- )
   ." There are " #FILES . ." files, and "
   DISK-FREE . ." bytes free." CR ;

GREETING
10 COUNTDOWN
FILES-HERE
