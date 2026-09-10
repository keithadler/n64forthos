OUTPUT_FORMAT("elf32-bigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80000400;

    .boot : { KEEP(*(.boot)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) . = ALIGN(8); }
    .data : { *(.data .data.*) . = ALIGN(8); }
    _gp = ALIGN(16) + 0x7ff0;
    .sdata : { *(.sdata .sdata.*) . = ALIGN(8); }

    __load_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    . = ALIGN(16);
    . += 0x8000;                    /* 32 KiB kernel stack */
    _stack_top = .;

    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.comment) *(.pdr) }
}
