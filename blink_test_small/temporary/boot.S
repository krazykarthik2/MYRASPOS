.section ".text.boot"
.global _start

_start:
    // do not  stack pointer to 0x80000 cause it is where it is located
    mov x0, #0x400000
mov sp, x0

    // Zero out BSS
    ldr x5, =__bss_start
    ldr x6, =__bss_end
    cmp x5, x6
    b.ge 2f
1:
    str xzr, [x5], #8
    cmp x5, x6
    b.lt 1b
2:

    // Jump to kernel_main
    bl kernel_main

    // If kernel_main returns, halt
halt:
    wfe
    b halt
