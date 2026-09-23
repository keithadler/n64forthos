\ ===========================================================================
\ tests.fth -- the kernel's own test suite.
\
\ Run at boot by the test ROM, a line at a time, the same way any Forth
\ source is read.  A line that raises an error loses that line and nothing
\ else, which is the point: several lines below are deliberate errors, and
\ the assertions after them have to keep passing.
\
\ IS compares the two values on top of the stack and counts the result.
\ tools/check.py reads the counts out of RDRAM after REPORT writes them.
\ ===========================================================================

VARIABLE PASSES
VARIABLE FAILS
VARIABLE TN
: CLEAR DEPTH 0 DO DROP LOOP ;
: IS 1 TN +! = IF 1 PASSES +! ELSE 1 FAILS +! ." FAIL test " TN @ . CR THEN ;

\ --- arithmetic ------------------------------------------------------------
2 3 + 5 IS
7 3 - 4 IS
6 7 * 42 IS
20 4 / 5 IS
17 5 MOD 2 IS
-20 4 / -5 IS
-17 5 MOD -2 IS
-7 ABS 7 IS
5 NEGATE -5 IS
3 9 MIN 3 IS
3 9 MAX 9 IS
5 1+ 6 IS
5 1- 4 IS
5 2* 10 IS
9 2/ 4 IS
0 1- -1 IS
32767 32767 + 65534 IS

\ --- bitwise ---------------------------------------------------------------
6 3 AND 2 IS
4 1 OR 5 IS
6 3 XOR 5 IS
0 INVERT -1 IS
-1 INVERT 0 IS
1 4 LSHIFT 16 IS
256 4 RSHIFT 16 IS
-1 1 RSHIFT 2147483647 IS

\ --- comparison ------------------------------------------------------------
3 3 = -1 IS
3 4 = 0 IS
3 4 < -1 IS
4 3 < 0 IS
4 3 > -1 IS
3 4 <> -1 IS
0 0= -1 IS
1 0= 0 IS
-1 0< -1 IS
1 0> -1 IS
-1 1 U< 0 IS

\ --- the stack -------------------------------------------------------------
DEPTH 0 IS
1 DUP + 2 IS
0 ?DUP DEPTH 1 IS CLEAR
1 ?DUP DEPTH 2 IS CLEAR
1 2 DROP 1 IS
1 2 SWAP DROP 2 IS
1 2 OVER NIP NIP 1 IS
1 2 3 ROT NIP NIP 1 IS
1 2 TUCK + + 5 IS
10 20 30 2 PICK 10 IS CLEAR
1 2 3 2 ROLL 1 IS CLEAR
5 >R R> 5 IS
7 >R R@ 7 IS R> DROP
DEPTH 0 IS

\ --- memory ----------------------------------------------------------------
VARIABLE V
99 V ! V @ 99 IS
5 V ! 3 V +! V @ 8 IS
42 CONSTANT ANSWER
ANSWER 42 IS
HERE 7 , HERE 4 - @ 7 IS
HERE 65 C, HERE 1- C@ 65 IS
V 4 CELLS + V - 16 IS
V CELL+ V - 4 IS

\ --- definitions -----------------------------------------------------------
: SQ DUP * ;
9 SQ 81 IS
: TWICE 2 * ;
: QUAD TWICE TWICE ;
3 QUAD 12 IS
: FIVE [ 2 3 + ] LITERAL ;
FIVE 5 IS
: RETURNS-3 1 2 + ;
RETURNS-3 3 IS

\ --- control flow ----------------------------------------------------------
: SGN DUP 0> IF DROP 1 ELSE 0< IF -1 ELSE 0 THEN THEN ;
5 SGN 1 IS
-5 SGN -1 IS
0 SGN 0 IS
: CNT 0 BEGIN 1+ DUP 5 = UNTIL ;
CNT 5 IS
: WR 0 BEGIN DUP 5 < WHILE 1+ REPEAT ;
WR 5 IS
: SUMN 0 SWAP 1+ 1 DO I + LOOP ;
5 SUMN 15 IS
100 SUMN 5050 IS
: NEST 0 3 0 DO 3 0 DO 1+ LOOP LOOP ;
NEST 9 IS
: OUTER 0 3 0 DO 2 0 DO J + LOOP LOOP ;
OUTER 6 IS

\ --- numbers and bases -----------------------------------------------------
$FF 255 IS
$10 16 IS
HEX 1F DECIMAL 31 IS
-1 ABS 1 IS

\ --- recursion -------------------------------------------------------------
\ A definition can see its own name, so recursion needs no special word.
: FACT DUP 1 > IF DUP 1- FACT * ELSE DROP 1 THEN ;
5 FACT 120 IS
10 FACT 3628800 IS
: FIB DUP 2 < IF DROP 1 ELSE DUP 1- FIB SWAP 2 - FIB + THEN ;
10 FIB 89 IS
15 FIB 987 IS

\ --- strings ---------------------------------------------------------------
S" ABC" NIP 3 IS
S" ABC" DROP C@ 65 IS
: MSG S" hello" ;
MSG NIP 5 IS
MSG DROP C@ 104 IS
MSG DROP 4 + C@ 111 IS

\ --- the dictionary --------------------------------------------------------
VARIABLE H0
HERE H0 !
1000 ALLOT
HERE H0 @ - 1000 IS
-1000 ALLOT
HERE H0 @ - 0 IS
: TEMP2 1 ;
TEMP2 1 IS
FORGET TEMP2
HERE H0 @ = -1 IS

\ --- colour and the drawing layer ------------------------------------------
255 255 255 RGB 65535 IS   \ a colour is 16 bits in a 32-bit cell
0 0 0 RGB 1 IS
0 255 0 RGB $07C1 IS
FB $A0200000 IS
SCREEN-W 640 IS
SCREEN-H 480 IS

\ --- the same arithmetic, but inside definitions ---------------------------
\ Everything above runs interpreted, one line at a time.  A definition is
\ compiled to machine code, and the two have to agree -- an inlined MIN once
\ returned the larger of the two and nothing here noticed.
: C-MIN 3 9 MIN ;      C-MIN 3 IS
: C-MIN2 9 3 MIN ;     C-MIN2 3 IS
: C-MAX 3 9 MAX ;      C-MAX 9 IS
: C-MAX2 9 3 MAX ;     C-MAX2 9 IS
: C-ADD 2 3 + ;        C-ADD 5 IS
: C-SUB 7 3 - ;        C-SUB 4 IS
: C-MUL 6 7 * ;        C-MUL 42 IS
: C-NEG 5 NEGATE ;     C-NEG -5 IS
: C-INV 0 INVERT ;     C-INV -1 IS
: C-NIP 1 2 NIP ;      C-NIP 2 IS
: C-2DROP 1 2 3 2DROP ;  C-2DROP 1 IS
: C-NE 3 4 <> ;        C-NE -1 IS
: C-EQ 4 4 = ;         C-EQ -1 IS
: C-LT 3 4 < ;         C-LT -1 IS
: C-GT 4 3 > ;         C-GT -1 IS
: C-ZLT -1 0< ;        C-ZLT -1 IS
: C-ZGT 1 0> ;         C-ZGT -1 IS
: C-ZEQ 0 0= ;         C-ZEQ -1 IS
: C-LSH 1 4 LSHIFT ;   C-LSH 16 IS
: C-RSH 256 4 RSHIFT ; C-RSH 16 IS
: C-RSTACK 5 >R R@ R> DROP ;   C-RSTACK 5 IS
: C-RSTACK2 7 >R R> ;  C-RSTACK2 7 IS
: C-FMUL 131072 98304 F* ;     C-FMUL 196608 IS
: C-FDIV 131072 65536 F/ ;     C-FDIV 131072 IS
: C-FSQRT 262144 FSQRT ;       C-FSQRT 131072 IS
: C-ABS -7 ABS ;       C-ABS 7 IS
: C-MOD 17 5 MOD ;     C-MOD 2 IS
: C-DIV 20 4 / ;       C-DIV 5 IS
: C-ROT 1 2 3 ROT ;    C-ROT 1 IS CLEAR
: C-VAR V @ ;          8 V ! C-VAR 8 IS
: C-LOOP 0 5 0 DO I + LOOP ;   C-LOOP 10 IS
: C-NEST 0 3 0 DO 3 0 DO 1+ LOOP LOOP ;  C-NEST 9 IS
: C-IF 5 0> IF 1 ELSE 2 THEN ; C-IF 1 IS
: C-IF2 -5 0> IF 1 ELSE 2 THEN ; C-IF2 2 IS
: C-BEGIN 0 BEGIN 1+ DUP 4 = UNTIL ;  C-BEGIN 4 IS
: C-EARLY DUP 0> IF DROP 1 EXIT THEN DROP 2 ;
5 C-EARLY 1 IS
-5 C-EARLY 2 IS

\ --- deliberate errors: each one must be survivable ------------------------
0 0 /
0 0 MOD
1 12345678 !
12345678 @
1 3 !
: BADIF IF ;
;
FORGET DUP
9999 PICK
NOSUCHWORD
600000 ALLOT
: PUSHY 400 0 DO I LOOP ;
PUSHY

\ --- and the system is still standing --------------------------------------
DEPTH 0 IS
HERE H0 !
: BADX IF ;
HERE H0 @ = -1 IS
: BADY 1 2 ;
BADY + 3 IS
2 2 + 4 IS
: AFTER-ERRORS 6 7 * ;
AFTER-ERRORS 42 IS
9 SQ 81 IS
: TEMPW 123 ;
TEMPW 123 IS
FORGET TEMPW
2 3 + 5 IS
DEPTH 0 IS

\ --- the rest of a Forth ----------------------------------------------------
\ Defining words, counted loops that may not run or that step, CASE.
: CONST CREATE , DOES> @ ;
42 CONST ANSWER
ANSWER 42 IS
: USE-ANSWER ANSWER 1+ ;
USE-ANSWER 43 IS
CREATE TBL 3 CELLS ALLOT
7 TBL !
TBL @ 7 IS
' TBL >BODY TBL = -1 IS
: ARRAY CREATE CELLS ALLOT DOES> SWAP CELLS + ;
5 ARRAY AR
99 3 AR !
3 AR @ 99 IS
: SUMTO ( n -- s ) 0 SWAP 0 ?DO I + LOOP ;
5 SUMTO 10 IS
0 SUMTO 0 IS
: EVENS 0 10 0 DO I + 2 +LOOP ;
EVENS 20 IS
: DOWNTO 0 0 10 DO I + -1 +LOOP ;
DOWNTO 55 IS
: NONE 0 5 5 ?DO 1+ 2 +LOOP ;
NONE 0 IS
: FACT DUP 1 > IF DUP 1- RECURSE * THEN ;
5 FACT 120 IS
CHAR A 65 IS
: BANG [CHAR] ! ;
BANG 33 IS
: PICKED CASE 1 OF 100 ENDOF 2 OF 200 ENDOF 999 SWAP ENDCASE ;
1 PICKED 100 IS
2 PICKED 200 IS
7 PICKED 999 IS
1 2 3 -ROT 2 IS 1 IS 3 IS
17 5 /MOD 3 IS 2 IS
100 3 7 */ 42 IS
5 0<> -1 IS
5 3 10 WITHIN -1 IS
10 3 10 WITHIN 0 IS
: FIRST>5 ( -- n ) 100 0 DO I 5 > IF I LEAVE THEN LOOP ;
FIRST>5 6 IS
: LEAVE-DOWN 0 0 10 DO I 3 = IF LEAVE THEN 1+ -1 +LOOP ;
LEAVE-DOWN 7 IS
: NESTED 0 3 0 DO 10 0 DO I 2 = IF LEAVE THEN 1+ LOOP LOOP ;
NESTED 6 IS
DEPTH 0 IS
: BADDOES DOES> ;
BADDOES
DEPTH 0 IS

\ --- files -----------------------------------------------------------------
\ The same words whatever the controller holds: a Controller Pak, or nothing
\ and so a RAM disk.  A pak that is not ours yet is formatted first.
: USABLE? DISK-STATE DUP 1 = SWAP 2 = OR ;
: READY USABLE? 0= IF (FORMAT) DROP THEN ;
READY
USABLE? -1 IS
VARIABLE FB2  64 ALLOT
S" hello, pak" FB2 SWAP CMOVE
FB2 10 S" T1.TXT" SAVE-FILE 0 IS
S" T1.TXT" FILE? -1 IS DROP 10 IS
FB2 64 0 FILL
FB2 64 S" T1.TXT" LOAD-FILE 0 IS 10 IS
FB2 C@ 104 IS
FB2 9 + C@ 107 IS
FB2 4 S" t1.txt" LOAD-FILE 0 IS 4 IS
S" T1.TXT" S" T2.TXT" RENAME-FILE 0 IS
S" T1.TXT" FILE? 0 IS
S" T2.TXT" FILE? -1 IS 2DROP
S" T2.TXT" DELETE-FILE 0 IS
S" T2.TXT" DELETE-FILE -1 IS
S" MANDEL.FTH" DELETE-FILE -5 IS
S" MANDEL.FTH" FILE? -1 IS 2 IS DROP
FB2 64 S" NOPE.TXT" LOAD-FILE -1 IS 0 IS
DISK-FREE H0 !
HERE 1000 S" BIG.TXT" SAVE-FILE 0 IS
DISK-FREE H0 @ 1024 - IS
HERE 1000 S" BIG.TXT" SAVE-FILE 0 IS
DISK-FREE H0 @ 1024 - IS
S" BIG.TXT" DELETE-FILE 0 IS
DISK-FREE H0 @ IS
FB2 5 S" HELLO.FTH" SAVE-FILE 0 IS
S" HELLO.FTH" FILE? -1 IS 2 = 0 IS 5 IS
S" HELLO.FTH" DELETE-FILE 0 IS
S" HELLO.FTH" FILE? -1 IS 2 IS DROP
S" 77 CONSTANT FROMFILE" FB2 SWAP CMOVE
FB2 20 S" INC.FTH" SAVE-FILE 0 IS
INCLUDE INC.FTH
FROMFILE 77 IS
S" INC.FTH" DELETE-FILE 0 IS
S" BAD NAME" FILE?
DEPTH 0 IS
INCLUDE NOSUCH.FTH
DEPTH 0 IS
2 3 + 5 IS

\ --- report ----------------------------------------------------------------
CR ." tests passed: " PASSES @ . CR
." tests failed: " FAILS @ . CR
PASSES @ FAILS @ REPORT
