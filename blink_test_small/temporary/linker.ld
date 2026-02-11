ENTRY(_start)

SECTIONS
{
    . = 0x00080000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;

    /* Force minimum image size to 4KB */
    .pad : {
        FILL(0x00);
        . = 0x00080000 + 4096;
    }

    __end = .;
}
