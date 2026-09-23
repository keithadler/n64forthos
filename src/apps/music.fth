\ music.fth -- a tune, played in the background.
\
\ Run it and the prompt comes straight back: the notes are queued
\ and the kernel feeds them to the audio interface once a frame,
\ whatever else you are doing.  QUIET stops them; 0 to 100 VOLUME
\ sets the level.  The tune is Beethoven's, from 1824.

\ Frequencies, in hertz, near enough.
196 CONSTANT G3   262 CONSTANT C4   294 CONSTANT D4
330 CONSTANT E4   349 CONSTANT F4   392 CONSTANT G4
440 CONSTANT A4   494 CONSTANT B4   523 CONSTANT C5

VARIABLE TEMPO  140 TEMPO !         \ ms to an eighth note

\ A note is held for most of its length and let go for the rest,
\ so two of the same pitch do not run together.
: NOTE ( hz eighths -- )
   TEMPO @ *  DUP 8 / >R  R@ - BEEP  0 R> BEEP ;

\ A tune is pairs of pitch and length, ended by a zero.
: PLAY ( addr -- )
   BEGIN DUP @ ?DUP WHILE
      OVER CELL+ @ NOTE  2 CELLS +
   REPEAT DROP ;

CREATE ODE
  E4 , 2 ,  E4 , 2 ,  F4 , 2 ,  G4 , 2 ,
  G4 , 2 ,  F4 , 2 ,  E4 , 2 ,  D4 , 2 ,
  C4 , 2 ,  C4 , 2 ,  D4 , 2 ,  E4 , 2 ,
  E4 , 3 ,  D4 , 1 ,  D4 , 4 ,
  E4 , 2 ,  E4 , 2 ,  F4 , 2 ,  G4 , 2 ,
  G4 , 2 ,  F4 , 2 ,  E4 , 2 ,  D4 , 2 ,
  C4 , 2 ,  C4 , 2 ,  D4 , 2 ,  E4 , 2 ,
  D4 , 3 ,  C4 , 1 ,  C4 , 4 ,
  0 ,

ODE PLAY
CYAN INK ." Playing the Ode to Joy.  QUIET stops it." CR WHITE INK
