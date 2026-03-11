.section ".text.boot"
.global _start

_start:
    // Park secondary cores: only allow core 0 to proceed
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbz x0, 2f
1:
    wfe
    b 1b
2:
    // Set temporary stack pointer for core 0 during EL switch
    ldr x1, =stack_top
    mov sp, x1

    // Check Current EL
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    b.ne el1_entry      // If not EL2 (e.g., already EL1), skip

    // We are in EL2. Set up EL1 execution environment.
    
    // Enable AArch64 in EL1 (hcr_el2.RW = 1)
    mov x0, #(1 << 31)  // RW bit
    msr hcr_el2, x0
    
    // Enable FPU/SIMD in EL1 (cpacr_el1)
    mov x0, #(3 << 20)
    msr cpacr_el1, x0
    
    // Set SP_EL1 to our stack
    ldr x1, =stack_top
    msr sp_el1, x1
    
    // Set up return environment in spsr_el2
    // Return to EL1h (SP_EL1), DAIF masked
    mov x0, #0x3C5
    msr spsr_el2, x0
    
    // Set return address to el1_entry
    adr x0, el1_entry
    msr elr_el2, x0
    
    // Perform the drop to EL1
    eret

el1_entry:
    // Now we are definitely in EL1
    // Set SP again just in case we didn't drop
    ldr x1, =stack_top
    mov sp, x1


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
