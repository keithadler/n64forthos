/* IPL3 - the 0xFC0-byte boot block the console runs out of RSP DMEM.
 *
 * On a real N64 the PIF copies ROM 0x000..0xFFF into SP DMEM and jumps to
 * 0xA4000040, which is where this lands.  All it has to do is get the kernel
 * image from the cartridge into RDRAM and jump to it - exactly what an
 * HLE-booting emulator does for us when it skips this block entirely, so both
 * paths hand the kernel the same machine.
 *
 * tools/mkrom.py stores the payload length at ROM 0xFF8, which is visible
 * here as DMEM 0xA4000FF8.
 */
    .set noreorder
    .set noat
    .section .ipl3, "ax", @progbits
    .global ipl3
ipl3:
    lui     $t0, 0xB000             /* src: cartridge, uncached */
    ori     $t0, $t0, 0x1000
    lui     $t1, 0xA000             /* dst: RDRAM 0x400, uncached */
    ori     $t1, $t1, 0x0400
    lui     $t2, 0xA400             /* payload length, stashed at DMEM 0xFF8 */
    lw      $t2, 0x0FF8($t2)
    addiu   $t2, $t2, 3             /* round up to a whole word */
    srl     $t2, $t2, 2

1:  lw      $t3, 0($t0)
    addiu   $t0, $t0, 4
    sw      $t3, 0($t1)
    addiu   $t2, $t2, -1
    bgtz    $t2, 1b
    addiu   $t1, $t1, 4             /* branch delay slot */

    /* Enter the kernel through the uncached alias.  At power-on the
     * instruction cache holds whatever it holds, and nothing has
     * invalidated it yet; fetching the first instructions cached could fetch
     * garbage.  The kernel switches itself to cached memory as soon as it
     * has emptied both caches. */
    lui     $t9, 0xA000
    ori     $t9, $t9, 0x0400
    jr      $t9
    nop
