/* The primitive numbers, shared by the interpreter and the code generator. */
#ifndef PRIMS_H
#define PRIMS_H

typedef s32 cell;                       /* the Forth cell, everywhere */

enum {
    P_DOCOL = 0, P_DOVAR, P_DOCON, P_EXIT, P_LIT, P_SLIT, P_BRANCH, P_ZBRANCH,
    P_DO, P_LOOP, P_I, P_J, P_LEAVE, P_UNLOOP,
    P_DUP, P_QDUP, P_DROP, P_SWAP, P_OVER, P_ROT, P_NIP, P_TUCK, P_PICK,
    P_2DUP, P_2DROP, P_2SWAP,
    P_DEPTH, P_TOR, P_RFROM, P_RFETCH, P_ROLL,
    P_ADD, P_SUB, P_MUL, P_DIV, P_MOD, P_NEGATE, P_ABS, P_MIN, P_MAX,
    P_1PLUS, P_1MINUS, P_2MUL, P_2DIV,
    P_AND, P_OR, P_XOR, P_INVERT, P_LSHIFT, P_RSHIFT,
    P_EQ, P_NE, P_LT, P_GT, P_ULT, P_ZEQ, P_ZLT, P_ZGT,
    P_FETCH, P_STORE, P_CFETCH, P_CSTORE, P_PLUSSTORE, P_FILL,
    P_DOT, P_UDOT, P_HDOT, P_DOTS, P_EMIT, P_CR, P_SPACE, P_SPACES, P_TYPE,
    P_HERE, P_ALLOT, P_COMMA, P_CCOMMA, P_CELLS, P_CELLPLUS,
    P_COLON, P_SEMI, P_IMMEDIATE, P_VARIABLE, P_CONSTANT, P_LITERAL,
    P_IF, P_ELSE, P_THEN, P_BEGIN, P_UNTIL, P_AGAIN, P_WHILE, P_REPEAT,
    P_DOSTR, P_PAREN, P_BACKSLASH, P_TICK, P_EXECUTE,
    P_DOIMM, P_LOOPIMM, P_FORGET, P_LBRACK, P_RBRACK,
    P_WORDS, P_SEE, P_DUMP, P_DECIMAL, P_HEX, P_BASE, P_ABORT,
    P_PAGE, P_AT, P_INK, P_RGBW, P_FRAMES, P_VSYNC, P_REPORT,
    /* the drawing layer: what libultra would have called the graphics API */
    P_FB, P_CLS, P_PLOT, P_BOX, P_FRAME, P_HLINE, P_VLINE, P_LINE,
    P_DRAWTEXT, P_BLIT, P_BLITKEY, P_SQUOTE, P_SQRUN,
    /* 16.16 fixed point, and where an app is allowed to draw */
    P_FMUL, P_FDIV, P_FSQRT, P_CANVASX, P_CANVASY, P_CANVASW, P_CANVASH,
    P_KEYSET, P_MOUSEX, P_MOUSEY, P_MOUSEB,
    /* what a window manager written in Forth needs from the kernel */
    P_CLIP, P_NOCLIP, P_SETCANVAS, P_POLL, P_BUTTONS, P_PRESSED,
    P_MOUSEHIT, P_CURSOR, P_HIDECUR, P_TICKS,
    /* files, and the prompt as a shell */
    P_PARSENAME, P_INCLUDE, P_EDIT, P_LOADFILE, P_SAVEFILE, P_DELFILE,
    P_RENFILE, P_FILEQ, P_NFILES, P_FILENTH, P_DISKFREE, P_DISKSTATE,
    P_FORMAT, P_MOUNT, P_DOTIOR, P_DOTVOL, P_UNUSED, P_KEY, P_INKEY, P_MS,
    P_CMOVE, P_RUN,
    /* the rest of a Forth: defining words, counted loops, CASE, input */
    P_CREATE, P_DOCREATE, P_DOES, P_PDOES, P_QDOIMM, P_QDO, P_PLOOPIMM,
    P_PLOOP, P_RECURSE, P_CHAR, P_BRCHAR, P_CASE, P_OF, P_ENDOF, P_ENDCASE,
    P_ACCEPT, P_TOBODY, P_LEAVEIMM,
    /* sound */
    P_BEEP, P_QUIET, P_SOUNDING, P_VOLUME, P_DOTPAREN, P_AGAINBR, P_FINDNAME
};

#endif /* PRIMS_H */
