.section .text
.global vectors
.global exception_entry

/* Save all registers on stack */
.macro kernel_entry
    sub sp, sp, #272
    stp x0, x1, [sp, #16 * 0]
    stp x2, x3, [sp, #16 * 1]
    stp x4, x5, [sp, #16 * 2]
    stp x6, x7, [sp, #16 * 3]
    stp x8, x9, [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x19, [sp, #16 * 9]
    stp x20, x21, [sp, #16 * 10]
    stp x22, x23, [sp, #16 * 11]
    stp x24, x25, [sp, #16 * 12]
    stp x26, x27, [sp, #16 * 13]
    stp x28, x29, [sp, #16 * 14]
    
    mrs x21, elr_el1
    mrs x22, spsr_el1
    stp x30, x21, [sp, #16 * 15] /* x30 at 240, ELR at 248 */
    str x22, [sp, #256]          /* SPSR at 256 */
.endm

/* Restore all registers from stack */
.macro kernel_exit
    ldp x30, x21, [sp, #16 * 15]
    ldr x22, [sp, #256]
    msr elr_el1, x21
    msr spsr_el1, x22
    
    ldp x0, x1, [sp, #16 * 0]
    ldp x2, x3, [sp, #16 * 1]
    ldp x4, x5, [sp, #16 * 2]
    ldp x6, x7, [sp, #16 * 3]
    ldp x8, x9, [sp, #16 * 4]
    ldp x10, x11, [sp, #16 * 5]
    ldp x12, x13, [sp, #16 * 6]
    ldp x14, x15, [sp, #16 * 7]
    ldp x16, x17, [sp, #16 * 8]
    ldp x18, x19, [sp, #16 * 9]
    ldp x20, x21, [sp, #16 * 10]
    ldp x22, x23, [sp, #16 * 11]
    ldp x24, x25, [sp, #16 * 12]
    ldp x26, x27, [sp, #16 * 13]
    ldp x28, x29, [sp, #16 * 14]
    add sp, sp, #272
    eret
.endm

.align 11
vectors:
    /* Current EL with SP0 */
    .align 7
    b sync_sp0
    .align 7
    b irq_sp0
    .align 7
    b fiq_sp0
    .align 7
    b serror_sp0

    /* Current EL with SPx */
    .align 7
    b sync_spx
    .align 7
    b irq_spx
    .align 7
    b fiq_spx
    .align 7
    b serror_spx

    /* Lower EL using AArch64 */
    .align 7
    b sync_el64
    .align 7
    b irq_el64
    .align 7
    b fiq_el64
    .align 7
    b serror_el64

    /* Lower EL using AArch32 */
    .align 7
    b sync_el32
    .align 7
    b irq_el32
    .align 7
    b fiq_el32
    .align 7
    b serror_el32

/* Handler wrappers */
sync_sp0:   kernel_entry; mov x0, #0; b handle_c
irq_sp0:    kernel_entry; mov x0, #1; b handle_c
fiq_sp0:    kernel_entry; mov x0, #2; b handle_c
serror_sp0: kernel_entry; mov x0, #3; b handle_c

sync_spx:   kernel_entry; mov x0, #4; b handle_c
irq_spx:    kernel_entry; mov x0, #5; b handle_c
fiq_spx:    kernel_entry; mov x0, #6; b handle_c
serror_spx: kernel_entry; mov x0, #7; b handle_c

sync_el64:  kernel_entry; mov x0, #8; b handle_c
irq_el64:   kernel_entry; mov x0, #9; b handle_c
fiq_el64:   kernel_entry; mov x0, #10; b handle_c
serror_el64: kernel_entry; mov x0, #11; b handle_c

sync_el32:  kernel_entry; mov x0, #12; b handle_c
irq_el32:   kernel_entry; mov x0, #13; b handle_c
fiq_el32:   kernel_entry; mov x0, #14; b handle_c
serror_el32: kernel_entry; mov x0, #15; b handle_c

handle_c:
    mrs x1, esr_el1
    mrs x2, elr_el1
    bl exception_c_handler
    kernel_exit
