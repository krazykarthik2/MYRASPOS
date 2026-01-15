.section ".text.boot"
.global _start
.code 32

_start:
    cpsid i

    ldr sp, =stack_top

    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
zero_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt zero_loop

    bl kernel_main

1:
    b 1b
