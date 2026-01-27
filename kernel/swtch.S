
.global cpu_switch_to
.global ret_from_fork

/*
 * void cpu_switch_to(struct task_context *prev, struct task_context *next);
 * x0 = prev
 * x1 = next
 *
 * Struct layout (matches sched.h):
 * x19..x30, sp
 */
cpu_switch_to:
    /* Save current context to prev */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80] /* x29=FP, x30=LR */
    
    mov x2, sp
    str x2, [x0, #96]

    /* Load next context */
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    
    ldr x2, [x1, #96]
    mov sp, x2

    ret

/*
 * ret_from_fork
 * Called when a new task starts for the first time.
 */
ret_from_fork:
    /* Enable IRQs for new task */
    msr daifclr, #2
    
    /* Call task function directly - no debug overhead */
    mov x0, x20    /* Argument to x0 */
    blr x19        /* Call task function */
    
    /* If task returns, kill it */
    mov x0, #0
    bl task_exit   /* You'll need to implement or expose task_exit/kill(self) */
    
    /* Should not reach here */
hang:
    b hang
