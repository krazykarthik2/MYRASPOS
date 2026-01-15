ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text : ALIGN(4)
    {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata*)
    }

    .data : ALIGN(4)
    {
        *(.data*)
    }

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    stack_top = . + 0x4000;
}
