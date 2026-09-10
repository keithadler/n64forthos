/* native.c -- turn a word's token thread into VR4300 machine code.
 *
 * The interpreter costs about thirty-six instructions per Forth word
 * executed: fetch the token, dispatch on it, do three lines of work.  This
 * walks the thread a word already has and writes out the instructions
 * directly, so a definition becomes a subroutine the CPU runs at its own
 * speed.  Nothing about the front end changes: the token thread stays where
 * it was, which is what SEE decompiles and what FORGET reclaims.
 *
 * The calling convention is one register wide.  A compiled word takes the
 * Forth stack pointer in $a0 and returns it in $v0, and keeps it in $t8
 * while it runs; the C helpers below take and return it the same way.  That
 * is why a call costs four instructions rather than a round trip through
 * memory.
 *
 * Anything the generator does not understand is not a failure: the word is
 * simply left interpreted, and the two live side by side.
 */
#include "n64.h"
#include "prims.h"

/* Registers */
#define R_AT 1
#define R_V0 2
#define R_V1 3
#define R_A0 4
#define R_A1 5
#define R_A2 6
#define R_S1 17                 /* the top of the stack, while running */
#define R_S2 18                 /* and the bottom */
#define R_T8 24                 /* the Forth stack pointer, while running */
#define R_T9 25
#define R_SP 29
#define R_RA 31

/* Instruction builders */
#define I_LUI(rt, imm)      (0x3C000000u | ((rt) << 16) | ((imm) & 0xFFFF))
#define I_ORI(rt, rs, imm)  (0x34000000u | ((rs) << 21) | ((rt) << 16) | ((imm) & 0xFFFF))
#define I_ADDIU(rt, rs, im) (0x24000000u | ((rs) << 21) | ((rt) << 16) | ((im) & 0xFFFF))
#define I_LW(rt, off, rs)   (0x8C000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_SW(rt, off, rs)   (0xAC000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_LBU(rt, off, rs)  (0x90000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_SB(rt, off, rs)   (0xA0000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_ADDU(rd, rs, rt)  (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x21)
#define I_SUBU(rd, rs, rt)  (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x23)
#define I_AND(rd, rs, rt)   (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x24)
#define I_OR(rd, rs, rt)    (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x25)
#define I_XOR(rd, rs, rt)   (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x26)
#define I_SLT(rd, rs, rt)   (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x2A)
#define I_SLTU(rd, rs, rt)  (((rs) << 21) | ((rt) << 16) | ((rd) << 11) | 0x2B)
#define I_SLL(rd, rt, sa)   (((rt) << 16) | ((rd) << 11) | ((sa) << 6) | 0x00)
#define I_SRL(rd, rt, sa)   (((rt) << 16) | ((rd) << 11) | ((sa) << 6) | 0x02)
#define I_SRA(rd, rt, sa)   (((rt) << 16) | ((rd) << 11) | ((sa) << 6) | 0x03)
#define I_MULT(rs, rt)      (((rs) << 21) | ((rt) << 16) | 0x18)
#define I_MFHI(rd)          (((rd) << 11) | 0x10)
#define I_MFLO(rd)          (((rd) << 11) | 0x12)
#define I_ANDI(rt, rs, imm) (0x30000000u | ((rs) << 21) | ((rt) << 16) | ((imm) & 0xFFFF))
#define I_SLTIU(rt, rs, im) (0x2C000000u | ((rs) << 21) | ((rt) << 16) | ((im) & 0xFFFF))
#define I_BEQ(rs, rt, off)  (0x10000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_BNE(rs, rt, off)  (0x14000000u | ((rs) << 21) | ((rt) << 16) | ((off) & 0xFFFF))
#define I_JAL(target)       (0x0C000000u | ((((u32)(target)) >> 2) & 0x03FFFFFFu))
#define I_JR(rs)            (((rs) << 21) | 0x08)
#define I_NOP               0x00000000u
#define I_MOVE(rd, rs)      I_ADDU(rd, rs, 0)

#define MAX_FIXUPS 256
#define MAX_LABELS 512

/* Helpers that live in forth.c, where the interpreter's state is. */
extern cell *fs_prim(cell *stack, cell code);
extern cell *fs_call(cell *stack, cell xt);
extern cell *fs_type(cell *stack, cell addr, cell len);
extern cell *fs_do(cell *stack);
extern cell *fs_index(cell *stack, cell level);
extern int fs_loop(void);
extern cell *fs_bad_stack(cell *stack);
extern int fs_aborted(void);
extern void icache_invalidate(void *addr, int bytes);

static u32 compiled_words;      /* how many definitions have machine code */
static u32 refused_words;       /* and how many the generator would not take */

u32 native_compiled(void) { return compiled_words; }
u32 native_refused(void) { return refused_words; }

static u32 *out;                /* where the next instruction goes */
static u32 *out_limit;
static int failed;

/* token address -> code address, for the branches */
static struct { cell token; u32 *code; } labels[MAX_LABELS];
static int nlabels;
static struct { u32 *at; cell token; } fixups[MAX_FIXUPS];
static int nfixups;
static u32 *epilogue_fixups[MAX_FIXUPS];
static int nepilogue;

static void emit(u32 insn)
{
    if (out >= out_limit) {
        failed = 1;
        return;
    }
    *(volatile u32 *)UNCACHED(out) = insn;
    out++;
}

static void emit_imm(int reg, cell v)
{
    if (v >= -32768 && v < 32768) {
        emit(I_ADDIU(reg, 0, v));
    } else if ((v & 0xFFFF) == 0) {
        emit(I_LUI(reg, (u32)v >> 16));
    } else {
        emit(I_LUI(reg, (u32)v >> 16));
        emit(I_ORI(reg, reg, (u32)v & 0xFFFF));
    }
}

static void mark_label(cell token)
{
    if (nlabels < MAX_LABELS) {
        labels[nlabels].token = token;
        labels[nlabels].code = out;
        nlabels++;
    } else {
        failed = 1;
    }
}

static void want_branch(u32 *at, cell token)
{
    if (nfixups < MAX_FIXUPS) {
        fixups[nfixups].at = at;
        fixups[nfixups].token = token;
        nfixups++;
    } else {
        failed = 1;
    }
}

static void want_epilogue(u32 *at)
{
    if (nepilogue < MAX_FIXUPS)
        epilogue_fixups[nepilogue++] = at;
    else
        failed = 1;
}

/* ------------------------------------------------------------ sequences */

/* Move the stack pointer and make sure it is still on the stack.  Two
 * instructions of checking per move is what keeps a compiled word from
 * writing over the dictionary when a program is wrong. */
static void emit_move_sp(int delta)
{
    u32 *at;

    emit(I_ADDIU(R_T8, R_T8, delta));
    if (delta > 0) {
        emit(I_SLTU(R_V0, R_S1, R_T8));
    } else {
        emit(I_SLTU(R_V0, R_T8, R_S2));
    }
    at = out;
    emit(I_BNE(R_V0, 0, 0));
    emit(I_NOP);
    want_branch(at, -1);                /* -1 is the bail-out */
}

static void emit_push_reg(int reg)
{
    emit(I_SW(reg, 0, R_T8));
    emit_move_sp(4);
}

/* Load a global into a register.  The offset in a load is sign extended, so
 * the upper half has to be rounded up when the lower half has its top bit
 * set -- the oldest trap in MIPS code generation, and it cost an afternoon. */
static void emit_load_global(int reg, u32 addr)
{
    u32 hi = (addr + 0x8000) >> 16;

    emit(I_LUI(reg, hi));
    emit(I_LW(reg, (int)(addr - (hi << 16)), reg));
}

/* Did the last helper raise an error?  Three instructions, rather than a
 * call to ask: this runs after every helper, so it is worth the inlining. */
static void emit_abort_check(void)
{
    u32 *at;

    emit_load_global(R_V0, forth_abort_flag());
    at = out;
    emit(I_BNE(R_V0, 0, 0));
    emit(I_NOP);
    want_epilogue(at);
}

static void emit_call(u32 addr, int check_abort)
{
    emit(I_MOVE(R_A0, R_T8));
    emit(I_JAL(addr));
    emit(I_NOP);
    emit(I_MOVE(R_T8, R_V0));
    if (check_abort)
        emit_abort_check();
}

/* An address a Forth program may reach: KSEG0 or KSEG1, and aligned.  The
 * interpreter checks the exact RDRAM size as well; compiled code leaves that
 * to the exception handler, which says what happened. */
static void emit_addr_check(int reg, int align)
{
    u32 *at;

    emit(I_SRL(R_V0, reg, 29));
    emit(I_ADDIU(R_V0, R_V0, -4));
    emit(I_SLTIU(R_V0, R_V0, 2));
    at = out;
    emit(I_BEQ(R_V0, 0, 0));            /* not KSEG0/KSEG1 -> bail */
    emit(I_NOP);
    want_branch(at, -1);                /* -1 marks the bail-out label */
    if (align > 1) {
        emit(I_ANDI(R_V0, reg, align - 1));
        at = out;
        emit(I_BNE(R_V0, 0, 0));
        emit(I_NOP);
        want_branch(at, -1);
    }
}

/* ---------------------------------------------------------- translation */

static int translate(cell xt, u32 *dest, int words, u32 **end)
{
    cell *ip = (cell *)(u32)(xt + 8);
    cell *thread = ip;
    int i;
    u32 *bail;

    out = dest;
    out_limit = dest + words;
    failed = 0;
    nlabels = nfixups = nepilogue = 0;

    /* Prologue: save what the C world expects kept, take the stack pointer,
     * and hold its two limits where the checks can reach them. */
    emit(I_ADDIU(R_SP, R_SP, -16));
    emit(I_SW(R_RA, 12, R_SP));
    emit(I_SW(R_S1, 8, R_SP));
    emit(I_SW(R_S2, 4, R_SP));
    emit(I_MOVE(R_T8, R_A0));
    emit_imm(R_S1, (cell)forth_stack_top());
    emit_imm(R_S2, (cell)forth_stack_base());

    /* The body. */
    ip = thread;
    for (i = 0; i < 4096 && !failed; i++) {
        cell tok_addr = (cell)(u32)ip;
        cell tok = *ip++;
        cell code = *(cell *)(u32)tok;

        mark_label(tok_addr);

        switch (code) {
        case P_EXIT:
            want_epilogue(out);
            emit(I_BEQ(0, 0, 0));
            emit(I_NOP);
            goto done;

        case P_LIT:
            emit_imm(R_AT, *ip++);
            emit_push_reg(R_AT);
            break;

        case P_DOCON:
            emit_imm(R_AT, *(cell *)(u32)(tok + 4));
            emit_push_reg(R_AT);
            break;

        case P_DOVAR:
            emit_imm(R_AT, tok + 4);
            emit_push_reg(R_AT);
            break;

        case P_DUP:
            emit(I_LW(R_AT, -4, R_T8));
            emit_push_reg(R_AT);
            break;

        case P_DROP:
            emit_move_sp(-4);
            break;

        case P_SWAP:
            emit(I_LW(R_AT, -4, R_T8));
            emit(I_LW(R_V0, -8, R_T8));
            emit(I_SW(R_V0, -4, R_T8));
            emit(I_SW(R_AT, -8, R_T8));
            break;

        case P_OVER:
            emit(I_LW(R_AT, -8, R_T8));
            emit_push_reg(R_AT);
            break;

        case P_2DUP:
            emit(I_LW(R_AT, -8, R_T8));
            emit(I_LW(R_V0, -4, R_T8));
            emit(I_SW(R_AT, 0, R_T8));
            emit(I_SW(R_V0, 4, R_T8));
            emit_move_sp(8);
            break;

        case P_ADD: case P_SUB: case P_AND: case P_OR: case P_XOR:
        case P_EQ: case P_LT: case P_GT: case P_ULT: {
            emit(I_LW(R_AT, -8, R_T8));
            emit(I_LW(R_V0, -4, R_T8));
            switch (code) {
            case P_ADD: emit(I_ADDU(R_AT, R_AT, R_V0)); break;
            case P_SUB: emit(I_SUBU(R_AT, R_AT, R_V0)); break;
            case P_AND: emit(I_AND(R_AT, R_AT, R_V0)); break;
            case P_OR:  emit(I_OR(R_AT, R_AT, R_V0)); break;
            case P_XOR: emit(I_XOR(R_AT, R_AT, R_V0)); break;
            case P_LT:  emit(I_SLT(R_AT, R_AT, R_V0));
                        emit(I_SUBU(R_AT, 0, R_AT)); break;
            case P_GT:  emit(I_SLT(R_AT, R_V0, R_AT));
                        emit(I_SUBU(R_AT, 0, R_AT)); break;
            case P_ULT: emit(I_SLTU(R_AT, R_AT, R_V0));
                        emit(I_SUBU(R_AT, 0, R_AT)); break;
            case P_EQ:  emit(I_XOR(R_AT, R_AT, R_V0));
                        emit(I_SLTIU(R_AT, R_AT, 1));
                        emit(I_SUBU(R_AT, 0, R_AT)); break;
            }
            emit(I_SW(R_AT, -8, R_T8));
            emit_move_sp(-4);
            break;
        }

        case P_MUL:
            emit(I_LW(R_AT, -8, R_T8));
            emit(I_LW(R_V0, -4, R_T8));
            emit(I_MULT(R_AT, R_V0));
            emit(I_MFLO(R_AT));
            emit(I_SW(R_AT, -8, R_T8));
            emit_move_sp(-4);
            break;

        case P_FMUL:                    /* (a*b) >> 16, through the 64-bit product */
            emit(I_LW(R_AT, -8, R_T8));
            emit(I_LW(R_V0, -4, R_T8));
            emit(I_MULT(R_AT, R_V0));
            emit(I_MFLO(R_AT));
            emit(I_MFHI(R_V0));
            emit(I_SRL(R_AT, R_AT, 16));
            emit(I_SLL(R_V0, R_V0, 16));
            emit(I_OR(R_AT, R_AT, R_V0));
            emit(I_SW(R_AT, -8, R_T8));
            emit_move_sp(-4);
            break;

        case P_1PLUS: case P_1MINUS:
            emit(I_LW(R_AT, -4, R_T8));
            emit(I_ADDIU(R_AT, R_AT, code == P_1PLUS ? 1 : -1));
            emit(I_SW(R_AT, -4, R_T8));
            break;

        case P_2MUL: case P_2DIV:
            emit(I_LW(R_AT, -4, R_T8));
            if (code == P_2MUL)
                emit(I_SLL(R_AT, R_AT, 1));
            else
                emit(I_SRA(R_AT, R_AT, 1));
            emit(I_SW(R_AT, -4, R_T8));
            break;

        case P_ZEQ:
            emit(I_LW(R_AT, -4, R_T8));
            emit(I_SLTIU(R_AT, R_AT, 1));
            emit(I_SUBU(R_AT, 0, R_AT));
            emit(I_SW(R_AT, -4, R_T8));
            break;

        case P_FETCH:
            emit(I_LW(R_AT, -4, R_T8));
            emit_addr_check(R_AT, 4);
            emit(I_LW(R_AT, 0, R_AT));
            emit(I_SW(R_AT, -4, R_T8));
            break;

        case P_STORE:
            emit(I_LW(R_AT, -4, R_T8));
            emit(I_LW(R_V1, -8, R_T8));
            emit_addr_check(R_AT, 4);
            emit(I_SW(R_V1, 0, R_AT));
            emit_move_sp(-8);
            break;

        case P_CFETCH:
            emit(I_LW(R_AT, -4, R_T8));
            emit_addr_check(R_AT, 1);
            emit(I_LBU(R_AT, 0, R_AT));
            emit(I_SW(R_AT, -4, R_T8));
            break;

        case P_CSTORE:
            emit(I_LW(R_AT, -4, R_T8));
            emit(I_LW(R_V1, -8, R_T8));
            emit_addr_check(R_AT, 1);
            emit(I_SB(R_V1, 0, R_AT));
            emit_move_sp(-8);
            break;

        case P_BRANCH: {
            cell target = *ip++;
            u32 *at = out;

            emit(I_BEQ(0, 0, 0));
            emit(I_NOP);
            want_branch(at, target);
            break;
        }

        case P_ZBRANCH: {
            cell target = *ip++;
            u32 *at;

            emit(I_LW(R_AT, -4, R_T8));
            emit_move_sp(-4);
            at = out;
            emit(I_BEQ(R_AT, 0, 0));
            emit(I_NOP);
            want_branch(at, target);
            break;
        }

        case P_DO:
            emit_call((u32)&fs_do, 0);
            break;

        case P_LOOP: {
            cell target = *ip++;
            u32 *at;

            emit(I_JAL((u32)&fs_loop));
            emit(I_NOP);
            at = out;
            emit(I_BNE(R_V0, 0, 0));
            emit(I_NOP);
            want_branch(at, target);
            break;
        }

        case P_I: case P_J: {           /* the loop index, off the return stack */
            u32 rsp_at = forth_rsp_addr();
            u32 rbase = forth_rstack_base();

            emit_load_global(R_V0, rsp_at);
            emit(I_SLL(R_V0, R_V0, 2));
            emit(I_LUI(R_V1, rbase >> 16));
            emit(I_ORI(R_V1, R_V1, rbase & 0xFFFF));
            emit(I_ADDU(R_V0, R_V0, R_V1));
            emit(I_LW(R_AT, code == P_I ? -4 : -12, R_V0));
            emit_push_reg(R_AT);
            break;
        }

        case P_SLIT: case P_SQRUN: {
            cell len = *ip++;
            cell addr = (cell)(u32)ip;

            ip += (len + 3) / 4;
            if (code == P_SQRUN) {
                emit_imm(R_AT, addr);
                emit_push_reg(R_AT);
                emit_imm(R_AT, len);
                emit_push_reg(R_AT);
            } else {
                emit(I_MOVE(R_A0, R_T8));
                emit_imm(R_A1, addr);
                emit_imm(R_A2, len);
                emit(I_JAL((u32)&fs_type));
                emit(I_NOP);
                emit(I_MOVE(R_T8, R_V0));
            }
            break;
        }

        case P_DOCOL: {                 /* a call to another definition */
            cell native = *(cell *)(u32)(tok + 4);

            emit(I_MOVE(R_A0, R_T8));
            if (native && native != (cell)(u32)dest) {
                emit(I_JAL((u32)native));
                emit(I_NOP);
            } else if (tok == xt) {     /* it is calling itself */
                emit(I_JAL((u32)dest));
                emit(I_NOP);
            } else {
                emit_imm(R_A1, tok);
                emit(I_JAL((u32)&fs_call));
                emit(I_NOP);
            }
            emit(I_MOVE(R_T8, R_V0));
            emit_abort_check();
            break;
        }

        /* Words that reach into the interpreter itself are left alone. */
        case P_EXECUTE: case P_WORDS: case P_SEE: case P_DUMP:
        case P_COLON: case P_SEMI: case P_LEAVE:
            return 0;

        default:                        /* everything else: call the primitive */
            emit(I_MOVE(R_A0, R_T8));
            emit_imm(R_A1, code);
            emit(I_JAL((u32)&fs_prim));
            emit(I_NOP);
            emit(I_MOVE(R_T8, R_V0));
            emit_abort_check();
            break;
        }
    }
done:
    if (failed)
        return 0;

    /* The bail-out for a stack that will not fit, and for a bad address. */
    bail = out;
    emit(I_MOVE(R_A0, R_T8));
    emit(I_JAL((u32)&fs_bad_stack));
    emit(I_NOP);
    emit(I_MOVE(R_T8, R_V0));

    /* Epilogue. */
    {
        u32 *epilogue = out;
        int n;

        emit(I_MOVE(R_V0, R_T8));
        emit(I_LW(R_RA, 12, R_SP));
        emit(I_LW(R_S1, 8, R_SP));
        emit(I_LW(R_S2, 4, R_SP));
        emit(I_ADDIU(R_SP, R_SP, 16));
        emit(I_JR(R_RA));
        emit(I_NOP);

        for (n = 0; n < nepilogue; n++) {
            u32 *at = epilogue_fixups[n];
            int off = (int)(epilogue - (at + 1));

            *(volatile u32 *)UNCACHED(at) =
                (*(volatile u32 *)UNCACHED(at) & 0xFFFF0000u) | (off & 0xFFFF);
        }
        /* branches inside the body */
        for (n = 0; n < nfixups; n++) {
            u32 *target = 0;
            int k;

            if (fixups[n].token == -1) {
                target = bail;
            } else {
                for (k = 0; k < nlabels; k++)
                    if (labels[k].token == fixups[n].token) {
                        target = labels[k].code;
                        break;
                    }
            }
            if (!target)
                return 0;               /* a branch we cannot resolve */
            {
                int off = (int)(target - (fixups[n].at + 1));
                volatile u32 *at = (volatile u32 *)UNCACHED(fixups[n].at);

                if (off < -32768 || off > 32767)
                    return 0;
                *at = (*at & 0xFFFF0000u) | (off & 0xFFFF);
            }
        }
    }

    *end = out;
    return 1;
}

/* Compile a colon definition, if we can.  Returns 1 if it now has code. */
int native_compile(cell xt)
{
    u32 *dest;
    u32 *end;
    int room;

    if (*(cell *)(u32)xt != P_DOCOL)
        return 0;
    dest = (u32 *)(u32)((forth_here() + 7) & ~7u);
    room = (int)((forth_limit() - (u32)dest) / 4) - 64;
    if (room < 64)
        return 0;

    if (!translate(xt, dest, room, &end)) {
        refused_words++;
        return 0;
    }

    icache_invalidate(dest, (int)((u8 *)end - (u8 *)dest));
    *(cell *)(u32)(xt + 4) = (cell)(u32)dest;     /* the word's native code */
    forth_set_here((u32)end);
    compiled_words++;
    return 1;
}
