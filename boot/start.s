/* AArch64 startup code */

.section .text.boot
.global _start
.type _start, %function

_start:
    /* ----------------------------------------------------
     * 1. Ensure we are in EL1
     * QEMU -machine virt starts in EL1 already,
     * but we defensively handle EL2 just in case.
     * ---------------------------------------------------- */

    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #1
    beq 1f

    /* If somehow in EL2, drop to EL1 */
    mov x0, #(1 << 31)        /* EL1h */
    msr spsr_el2, x0
    adr x0, 1f
    msr elr_el2, x0
    eret

1:
    /* ----------------------------------------------------
     * 2. Disable interrupts
     * ---------------------------------------------------- */
    msr daifset, #0xf

    /* ----------------------------------------------------
     * 3. Set up stack
     * Stack must be 16-byte aligned in AArch64
     * ---------------------------------------------------- */
    ldr x0, =stack_top
    mov sp, x0

    /* ----------------------------------------------------
     * 4. Zero BSS
     * ---------------------------------------------------- */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    mov x2, #0

bss_clear:
    cmp x0, x1
    b.ge bss_done
    str x2, [x0], #8
    b bss_clear

bss_done:
    /* ----------------------------------------------------
     * 5. Jump to C kernel
     * ---------------------------------------------------- */
    bl kernel_main

halt:
    wfi
    b halt
