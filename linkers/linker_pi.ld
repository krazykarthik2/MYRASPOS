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
    . = . + 0x10000; /* 64KB stack */
    stack_top = .;
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;

    __end = .;
}
