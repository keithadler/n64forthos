/* Kernel entry.  Runs at 0x80000400 with nothing set up but RDRAM. */
    .set noreorder
    .set noat
    .set mips3                      /* for the cache instruction */
    .section .boot, "ax", @progbits
    .global _start
_start:
    mtc0    $zero, $12              /* Status: kernel mode, no interrupts, BEV=0 */
    nop
    mtc0    $zero, $13              /* Cause */
    nop

    /* Invalidate both caches (index store tag with TagLo = 0). */
    mtc0    $zero, $28              /* TagLo */
    nop
    lui     $t0, 0x8000
    ori     $t1, $zero, 512         /* I-cache: 16 KiB / 32-byte lines */
1:  cache   0x08, 0($t0)
    addiu   $t1, $t1, -1
    bgtz    $t1, 1b
    addiu   $t0, $t0, 32
    lui     $t0, 0x8000
    ori     $t1, $zero, 512         /* D-cache: 8 KiB / 16-byte lines */
2:  cache   0x09, 0($t0)
    addiu   $t1, $t1, -1
    bgtz    $t1, 2b
    addiu   $t0, $t0, 16

    /* Both caches are empty now, so it is safe to run cached.  This jumps
     * to the link-time address of the label, which is in KSEG0; the code has
     * been running from the same instructions through KSEG1 until now. */
    lui     $t9, %hi(cached_entry)
    ori     $t9, $t9, %lo(cached_entry)
    jr      $t9
    nop
cached_entry:

    /* Stack and global pointer. */
    lui     $sp, %hi(_stack_top)
    ori     $sp, $sp, %lo(_stack_top)
    lui     $gp, %hi(_gp)
    ori     $gp, $gp, %lo(_gp)

    /* Zero .bss. */
    lui     $t0, %hi(__bss_start)
    ori     $t0, $t0, %lo(__bss_start)
    lui     $t1, %hi(__bss_end)
    ori     $t1, $t1, %lo(__bss_end)
3:  sltu    $at, $t0, $t1
    beq     $at, $zero, 4f
    nop
    sw      $zero, 0($t0)
    j       3b
    addiu   $t0, $t0, 4
4:
    jal     kmain
    nop
5:  j       5b
    nop

/* Landing pad for CPU exceptions; kernel.c points 0x80000180 & friends here. */
    .global _exc_entry
_exc_entry:
    lui     $sp, %hi(_stack_top)
    ori     $sp, $sp, %lo(_stack_top)
    mfc0    $a0, $13                /* Cause */
    mfc0    $a1, $14                /* EPC */
    mfc0    $a2, $8                 /* BadVAddr */
    jal     exception_handler
    nop
6:  j       6b
    nop

/* icache_invalidate(void *addr, int bytes) -- the code we just wrote was
 * written through the uncached alias, so nothing is sitting dirty in the
 * data cache; the instruction cache is what has to be told. */
    .global icache_invalidate
icache_invalidate:
    blez    $a1, 2f
    nop
1:  cache   0x10, 0($a0)            /* hit invalidate, instruction cache */
    addiu   $a1, $a1, -32
    bgtz    $a1, 1b
    addiu   $a0, $a0, 32
2:  jr      $ra
    nop
