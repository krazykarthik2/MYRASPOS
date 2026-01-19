.section .text
.global vectors
.global exception_entry

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
/* x0 = type (0-3 for SP0, 4-7 for SPx, 8-11 for EL64, 12-15 for EL32) */

sync_sp0:   mov x0, #0
            b handle
irq_sp0:    mov x0, #1
            b handle
fiq_sp0:    mov x0, #2
            b handle
serror_sp0: mov x0, #3
            b handle

sync_spx:   mov x0, #4
            b handle
irq_spx:    mov x0, #5
            b handle
fiq_spx:    mov x0, #6
            b handle
serror_spx: mov x0, #7
            b handle

sync_el64:  mov x0, #8
            b handle
irq_el64:   mov x0, #9
            b handle
fiq_el64:   mov x0, #10
            b handle
serror_el64: mov x0, #11
            b handle

sync_el32:  mov x0, #12
            b handle
irq_el32:   mov x0, #13
            b handle
fiq_el32:   mov x0, #14
            b handle
serror_el32: mov x0, #15
            b handle

handle:
    /* Verify stack alignment (optional but good practice) here, 
       but for panic we just dump registers. 
       We need to save context if we were to return, 
       but for now we assume we just panic.
       However, we need to pass ESR and ELR to C.
    */
    mrs x1, esr_el1
    mrs x2, elr_el1
    
    /* Call C handler: void exception_c_handler(int type, uint64_t esr, uint64_t elr) */
    bl exception_c_handler
    
    /* If handler returns, just hang */
    b .
