# Exception Vectors Documentation (vectors.S)

## Overview

The exception vectors (`vectors.S`) provide the low-level exception handling infrastructure for MYRASPOS on ARM64 (AArch64) architecture. This assembly code implements:
- ARM64 exception vector table (16 entry points)
- Context save/restore macros for all 31 general-purpose registers
- Exception routing to C handlers
- Support for all exception levels and types
- Minimal overhead exception entry/exit

The vectors form the critical bridge between hardware exceptions (interrupts, syscalls, faults) and kernel exception handlers, ensuring proper CPU state preservation and restoration.

## ARM64 Exception Model

### Exception Levels (EL)
ARM64 defines 4 privilege levels:
- **EL0:** Unprivileged (userspace) - Currently unused in MYRASPOS
- **EL1:** Privileged (kernel mode) - MYRASPOS operates here
- **EL2:** Hypervisor - Not used
- **EL3:** Secure monitor - Not used

### Exception Types
Four types of exceptions exist:
1. **Synchronous:** Instruction-caused (syscalls, faults, undefined instructions)
2. **IRQ:** Normal interrupt request (timer, UART, peripherals)
3. **FIQ:** Fast interrupt request (high-priority interrupts)
4. **SError:** System error (asynchronous abort)

### Exception Sources
Exceptions can come from:
- **Current EL with SP0:** Using SP_EL0 stack pointer
- **Current EL with SPx:** Using SP_ELx stack pointer (most common in kernel)
- **Lower EL (AArch64):** 64-bit mode userspace (future use)
- **Lower EL (AArch32):** 32-bit mode userspace (not supported)

## Vector Table Structure

### Vector Table Layout
The ARM64 architecture requires a 2KB-aligned vector table with 16 entries, each spaced 128 bytes (0x80) apart.

```
Offset    Exception Source           Exception Type
------    ----------------           --------------
0x000     Current EL with SP0        Synchronous
0x080     Current EL with SP0        IRQ
0x100     Current EL with SP0        FIQ
0x180     Current EL with SP0        SError

0x200     Current EL with SPx        Synchronous
0x280     Current EL with SPx        IRQ
0x300     Current EL with SPx        FIQ
0x380     Current EL with SPx        SError

0x400     Lower EL (AArch64)         Synchronous
0x480     Lower EL (AArch64)         IRQ
0x500     Lower EL (AArch64)         FIQ
0x580     Lower EL (AArch64)         SError

0x600     Lower EL (AArch32)         Synchronous
0x680     Lower EL (AArch32)         IRQ
0x700     Lower EL (AArch32)         FIQ
0x780     Lower EL (AArch32)         SError
```

**Implementation:**
```asm
.align 11              // 2^11 = 2048 byte alignment
vectors:
    .align 7           // Each entry 128 bytes apart
    b sync_sp0         // 0x000
    .align 7
    b irq_sp0          // 0x080
    .align 7
    b fiq_sp0          // 0x100
    // ... continues for all 16 entries
```

### Why 2KB Alignment?
ARM64 requires VBAR_EL1 (Vector Base Address Register) bits [10:0] to be zero, enforcing 2KB alignment. This allows hardware to quickly compute exception entry point:
```
Exception Address = VBAR_EL1 + (exception_type × 0x80)
```

## Assembly Macros

### kernel_entry Macro
Saves complete CPU context onto the kernel stack.

```asm
.macro kernel_entry
    sub sp, sp, #272           // Allocate 272 bytes on stack
    stp x0, x1, [sp, #16 * 0]  // Save x0, x1 at offset 0
    stp x2, x3, [sp, #16 * 1]  // Save x2, x3 at offset 16
    // ... save x4-x29 ...
    
    mrs x21, elr_el1           // Read Exception Link Register
    mrs x22, spsr_el1          // Read Saved Program Status Register
    stp x30, x21, [sp, #16 * 15]  // Save LR and ELR
    str x22, [sp, #256]        // Save SPSR
.endm
```

**Stack Frame Layout (272 bytes):**
```
Offset    Register(s)      Size
------    -----------      ----
0         x0, x1           16 bytes
16        x2, x3           16 bytes
32        x4, x5           16 bytes
48        x6, x7           16 bytes
64        x8, x9           16 bytes
80        x10, x11         16 bytes
96        x12, x13         16 bytes
112       x14, x15         16 bytes
128       x16, x17         16 bytes
144       x18, x19         16 bytes
160       x20, x21         16 bytes
176       x22, x23         16 bytes
192       x24, x25         16 bytes
208       x26, x27         16 bytes
224       x28, x29         16 bytes
240       x30 (LR)         8 bytes
248       ELR_EL1          8 bytes
256       SPSR_EL1         8 bytes
264       (padding)        8 bytes
Total: 272 bytes
```

**Register Pairs:** Uses `stp` (store pair) for efficiency - saves two 64-bit registers in one instruction.

**Special Registers:**
- **ELR_EL1:** Exception Link Register - return address after exception
- **SPSR_EL1:** Saved Program Status Register - CPU state before exception
- **x30 (LR):** Link Register - return address for function calls

### kernel_exit Macro
Restores CPU context and returns from exception.

```asm
.macro kernel_exit
    ldp x30, x21, [sp, #16 * 15]  // Restore LR and ELR
    ldr x22, [sp, #256]            // Restore SPSR
    msr elr_el1, x21               // Write back ELR
    msr spsr_el1, x22              // Write back SPSR
    
    ldp x0, x1, [sp, #16 * 0]      // Restore x0, x1
    // ... restore x2-x29 ...
    
    add sp, sp, #272               // Deallocate stack frame
    eret                           // Exception Return
.endm
```

**eret Instruction:**
- Restores PC from ELR_EL1 (jumps to return address)
- Restores PSTATE from SPSR_EL1 (CPU flags, mode, etc.)
- Atomically returns from exception level

**Order Matters:**
1. Restore ELR/SPSR first (before clobbering x21/x22)
2. Restore general registers
3. Deallocate stack
4. Execute `eret`

## Exception Entry Points

### Current EL with SP0 Handlers
```asm
sync_sp0:   kernel_entry; mov x0, #0; b handle_c
irq_sp0:    kernel_entry; mov x0, #1; b handle_c
fiq_sp0:    kernel_entry; mov x0, #2; b handle_c
serror_sp0: kernel_entry; mov x0, #3; b handle_c
```

**Usage:** Exceptions while using SP_EL0 (rare, typically not used in MYRASPOS).

**Exception IDs:** 0-3 passed in x0 to identify exception type.

### Current EL with SPx Handlers
```asm
sync_spx:   kernel_entry; mov x0, #4; b handle_c
irq_spx:    kernel_entry; mov x0, #5; b handle_c
fiq_spx:    kernel_entry; mov x0, #6; b handle_c
serror_spx: kernel_entry; mov x0, #7; b handle_c
```

**Usage:** Exceptions while running in kernel mode with SP_EL1 (most common).

**Exception IDs:** 4-7.

**Typical Cases:**
- Timer IRQs during kernel execution
- Synchronous exceptions from kernel code bugs
- UART interrupts

### Lower EL (AArch64) Handlers
```asm
sync_el64:  kernel_entry; mov x0, #8; b handle_c
irq_el64:   kernel_entry; mov x0, #9; b handle_c
fiq_el64:   kernel_entry; mov x0, #10; b handle_c
serror_el64: kernel_entry; mov x0, #11; b handle_c
```

**Usage:** Exceptions from 64-bit userspace (future use).

**Exception IDs:** 8-11.

**Future:** Will handle syscalls (SVC instruction) from userspace.

### Lower EL (AArch32) Handlers
```asm
sync_el32:  kernel_entry; mov x0, #12; b handle_c
irq_el32:   kernel_entry; mov x0, #13; b handle_c
fiq_el32:   kernel_entry; mov x0, #14; b handle_c
serror_el32: kernel_entry; mov x0, #15; b handle_c
```

**Usage:** Exceptions from 32-bit userspace (not supported).

**Exception IDs:** 12-15.

**Status:** Placeholder, not used in MYRASPOS.

## C Handler Dispatch

### handle_c Entry Point
Common dispatch point for all exceptions.

```asm
handle_c:
    mov x3, sp                 // Pass stack pointer as 4th argument
    mrs x1, esr_el1           // Read Exception Syndrome Register
    mrs x2, elr_el1           // Read Exception Link Register
    bl exception_c_handler    // Call C handler
    kernel_exit               // Restore context and return
```

**Arguments to C Handler:**
- **x0:** Exception type ID (0-15)
- **x1:** ESR_EL1 (exception cause and details)
- **x2:** ELR_EL1 (exception address)
- **x3:** Stack pointer (pointer to saved context)

**C Function Signature:**
```c
void exception_c_handler(uint32_t type, uint64_t esr, 
                        uint64_t elr, void *sp);
```

**Flow:**
1. Save context via `kernel_entry`
2. Load exception information from system registers
3. Call C handler with all context
4. C handler processes exception (may modify saved context)
5. Return from C handler
6. Restore context via `kernel_exit`
7. Resume execution at ELR_EL1

## Exception Syndrome Register (ESR_EL1)

The ESR_EL1 provides detailed exception information:

**Bit Fields:**
- **[31:26]:** EC (Exception Class) - Type of exception
- **[25]:** IL (Instruction Length) - 16-bit or 32-bit instruction
- **[24:0]:** ISS (Instruction Specific Syndrome) - Additional info

**Common Exception Classes (EC):**
- `0x15` (21): SVC instruction (syscall)
- `0x20` (32): Instruction abort from lower EL
- `0x21` (33): Instruction abort from same EL
- `0x24` (36): Data abort from lower EL
- `0x25` (37): Data abort from same EL
- `0x00` (0): Unknown reason
- `0x0E` (14): Illegal execution state

**Example ESR Parsing (in C):**
```c
uint32_t ec = (esr >> 26) & 0x3F;
if (ec == 0x15) {
    // SVC (syscall)
    uint32_t imm = esr & 0xFFFF;  // Immediate value
}
```

## Implementation Details

### Stack Frame Size Calculation
```
Registers to save:
- x0-x29: 30 registers × 8 bytes = 240 bytes
- x30 (LR): 8 bytes
- ELR_EL1: 8 bytes
- SPSR_EL1: 8 bytes
- Padding for alignment: 8 bytes
Total: 272 bytes
```

**Alignment:** 16-byte aligned (required by AAPCS64).

### Why Save All Registers?
**Decision:** Save entire register file (x0-x30 + ELR + SPSR).

**Rationale:**
- C handlers may use any register (via compiler)
- System registers (ELR, SPSR) must be preserved
- Ensures complete context switch capability
- Simplifies context switching for scheduler

**Cost:** ~50 cycles for save + ~50 cycles for restore (~100ns total @ 1GHz).

### Why Use Macros?
**Decision:** Implement save/restore as assembler macros.

**Benefits:**
- Code reuse across all 16 exception vectors
- Consistent context handling
- Easier to maintain (single definition)
- Compiler-like behavior (inline expansion)

### Register Usage Convention
**Scratch Registers (x21, x22):**
- Used temporarily to hold ELR/SPSR during save/restore
- Themselves saved to stack before use
- Restored before returning

**Why x21/x22?**
- Callee-saved in AAPCS64 (not used by C code without saving)
- High enough to avoid common temporary register use
- Paired for efficient store/load instructions

### Stack Pointer Choice
**Current Implementation:** Uses SP_EL1 (kernel stack).

**Why SP_EL1?**
- All kernel code runs with SP_EL1
- Separate from user stack (when EL0 added)
- Dedicated kernel stack per task

**Stack Layout:**
```
High Address
    ↓
[Kernel Stack]
    ↓ (grows down)
[Exception Frame] ← SP after kernel_entry
    ↓
Low Address
```

## Execution Flow Example

### Timer IRQ While in Kernel
```
1. Timer fires, CPU takes IRQ exception
   ↓
2. Hardware:
   - Saves PC to ELR_EL1
   - Saves PSTATE to SPSR_EL1  
   - Disables interrupts (DAIF = 0xF)
   - Jumps to vectors + 0x280 (IRQ, SPx)
   ↓
3. Assembly (vectors.S):
   - irq_spx entry point
   - kernel_entry macro:
     * Allocates 272 bytes on stack
     * Saves x0-x30, ELR, SPSR
   - mov x0, #5 (exception type)
   - b handle_c
   ↓
4. handle_c:
   - mov x3, sp (pass stack pointer)
   - mrs x1, esr_el1 (read exception cause)
   - mrs x2, elr_el1 (read exception PC)
   - bl exception_c_handler
   ↓
5. C Handler (exception.c):
   - exception_c_handler(5, esr, elr, sp)
   - Identifies IRQ type
   - Calls timer_handle_irq()
   - Returns
   ↓
6. Assembly (vectors.S):
   - kernel_exit macro:
     * Restores ELR_EL1, SPSR_EL1
     * Restores x0-x30
     * Deallocates stack frame
   - eret
   ↓
7. Hardware:
   - Restores PC from ELR_EL1
   - Restores PSTATE from SPSR_EL1
   - Re-enables interrupts
   - Continues execution where interrupted
```

### Syscall from Kernel (Future EL0 Support)
```
1. User code executes: svc #0
   ↓
2. Hardware:
   - Exception to EL1
   - Saves user PC to ELR_EL1
   - Saves user PSTATE to SPSR_EL1
   - Jumps to vectors + 0x400 (Sync, Lower EL64)
   ↓
3. Assembly:
   - sync_el64 entry point
   - kernel_entry (saves user context)
   - mov x0, #8
   - b handle_c
   ↓
4. handle_c → C handler:
   - Checks ESR: EC == 0x15 (SVC)
   - Extracts syscall number from ISS
   - Calls syscall_handle(num, x0, x1, x2)
   - Puts return value in saved x0
   ↓
5. Assembly:
   - kernel_exit (restores user context with new x0)
   - eret (returns to user code)
```

## Design Decisions

### Why Single Entry Per Exception?
**Decision:** Each vector entry is a single branch instruction.

**Rationale:**
- Fits in 128-byte slot requirement
- Consistent handler structure
- Easy to trace execution flow
- Allows space for future per-vector logic

**Alternative:** Could have inline handlers, but would limit size.

### Why Branch to Common handle_c?
**Decision:** All exception types route through `handle_c`.

**Rationale:**
- Centralizes exception information gathering
- Single call site to C code
- Consistent register setup
- Simplifies debugging (single breakpoint catches all)

**Trade-off:** Slight overhead vs. direct per-type handlers (negligible).

### Why Save to Stack vs. Dedicated Area?
**Decision:** Save context on current kernel stack.

**Rationale:**
- Automatic per-task context isolation
- No need for global context buffer
- Supports nested exceptions (if re-enabled)
- Natural for context switching

**Alternative:** Per-CPU context save area would require extra management.

### Why 272 Bytes?
**Decision:** Allocate 272 bytes for exception frame.

**Calculation:**
- 30 GP registers (x0-x29): 240 bytes
- LR (x30): 8 bytes
- ELR_EL1: 8 bytes
- SPSR_EL1: 8 bytes
- Padding: 8 bytes (for 16-byte alignment)

**Future:** Could add FP/SIMD registers (q0-q31) if needed (~512 more bytes).

### Why Not Save FP Registers?
**Decision:** Don't save floating-point/SIMD registers (q0-q31).

**Rationale:**
- MYRASPOS kernel doesn't use FP (currently)
- Saves ~512 bytes stack space
- Saves ~100 cycles per exception
- Can add if userspace needs it

**Implication:** User code using FP must not rely on values across syscalls (unless we add lazy FP save).

## Performance Characteristics

### Exception Entry Overhead
**Assembly Instructions:**
- `kernel_entry`: ~30 instructions (mostly `stp`)
- Register reads: 2 instructions (`mrs`)
- Branch: 1 instruction (`bl`)
- **Total:** ~33 instructions

**Estimated Latency:**
- Assembly: ~33 cycles
- Cache effects: +10-20 cycles (if stack misses)
- **Total:** ~40-50 cycles (~40-50ns @ 1GHz)

### Exception Exit Overhead
**Assembly Instructions:**
- Register restore: ~30 instructions (mostly `ldp`)
- Register writes: 2 instructions (`msr`)
- Return: 1 instruction (`eret`)
- **Total:** ~33 instructions (~33 cycles)

**Total Round-Trip:** ~70-100 cycles per exception.

### Memory Footprint
**Code Size:**
- Vector table: 2KB (alignment requirement, ~500 bytes actual code)
- Macros: Inlined into each vector (~50 bytes × 16 = 800 bytes)
- **Total:** ~2.5KB

**Runtime Memory:**
- Stack frame: 272 bytes per exception (temporary)

## Error Handling

### Unexpected Exceptions
All exceptions route to C handler, which can:
- Log exception details (type, ESR, ELR)
- Dump register state from stack frame
- Panic or attempt recovery

**Example (in exception.c):**
```c
if (type == 7) {  // SError
    uart_puts("PANIC: System Error\n");
    uart_puts("ESR: "); uart_put_hex(esr);
    uart_puts("\nELR: "); uart_put_hex(elr);
    panic();
}
```

### Double Faults
If exception occurs during exception handling:
- Hardware may enter recursive exception
- Stack overflow possible if stack space exhausted
- Current implementation: No special handling

**Mitigation:** Ensure exception handlers don't fault (careful programming).

### Stack Overflow
If exception occurs with insufficient stack space:
- `kernel_entry` may corrupt kernel data
- Behavior undefined

**Protection:** Guard pages not implemented (future enhancement).

## Cross-References

### Related Components
- **exception.c/h** - C-level exception dispatcher and handlers
  - `exception_c_handler()` - Main C entry point
  - Syscall dispatch logic
  - IRQ routing
- **syscall.c/h** - Syscall handling (invoked from sync exception)
- **irq.c/h** - Interrupt management and device IRQ handlers
- **timer.c/h** - Timer interrupt handling
- **sched.c/h** - Context switching (uses similar save/restore)

### Related Services
- **[syscall.md](syscall.md)** - Syscall interface (sync exceptions)
- **[shell.md](shell.md)** - Shell may trigger exceptions indirectly
- **[service.md](service.md)** - Services run in exception context
- **[pty.md](pty.md)** - PTY I/O may occur in IRQ handlers

### System Registers
- **VBAR_EL1:** Vector Base Address Register - points to this table
- **ESR_EL1:** Exception Syndrome Register - exception cause
- **ELR_EL1:** Exception Link Register - return address
- **SPSR_EL1:** Saved Program Status Register - saved PSTATE
- **SP_EL1:** Stack Pointer (EL1) - kernel stack
- **SP_EL0:** Stack Pointer (EL0) - user stack (future)

### ARM64 References
- **ARM Architecture Reference Manual (ARMv8):** Authoritative spec
- **Exception Model:** Chapter D1 (ARM ARM)
- **Vector Table:** Section D1.10.2
- **Exception Entry:** Section D1.10.3
- **Exception Return (ERET):** Section C5.6.50

## Future Enhancements

### Planned Improvements
1. **FP/SIMD context save:** When userspace added
2. **Stack overflow detection:** Guard pages or canaries
3. **Exception statistics:** Count exceptions by type
4. **Nested exception support:** Re-enable interrupts in handlers
5. **Per-CPU vectors:** For SMP support

### Userspace Support
When EL0 userspace is added:
- Lower EL handlers (8-11) will activate
- Need separate kernel/user stacks
- Syscall path via sync_el64 will handle SVC instructions
- Context switch will save/restore user registers

### SMP Support
For multi-core:
- Per-CPU exception stacks
- Per-CPU VBAR_EL1 (or shared vector table)
- Spinlocks in C handlers for shared data
- IRQ routing and affinity

### Debug Features
```asm
// Add exception statistics
exception_stats:
    .quad 0  // Exception type 0 count
    .quad 0  // Exception type 1 count
    // ... etc

sync_sp0:
    kernel_entry
    adr x9, exception_stats
    ldr x10, [x9]
    add x10, x10, #1
    str x10, [x9]
    mov x0, #0
    b handle_c
```

## Testing and Debugging

### Triggering Exceptions Manually

**Undefined Instruction:**
```c
asm volatile(".word 0xFFFFFFFF");  // Undefined opcode
```

**Data Abort:**
```c
*(volatile int *)0 = 42;  // NULL pointer dereference
```

**Syscall:**
```c
asm volatile("svc #0");  // Trigger SVC exception
```

### Verifying Vector Table Setup
```c
// Read VBAR_EL1
uint64_t vbar;
asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
uart_puts("VBAR_EL1: "); uart_put_hex(vbar);

// Should point to 'vectors' symbol
extern void *vectors;
uart_puts("\nvectors addr: "); uart_put_hex((uint64_t)&vectors);
```

### Dumping Exception Context
```c
void exception_c_handler(uint32_t type, uint64_t esr, 
                        uint64_t elr, void *sp) {
    uint64_t *regs = (uint64_t *)sp;
    uart_puts("Exception type: "); uart_put_hex(type);
    uart_puts("\nESR: "); uart_put_hex(esr);
    uart_puts("\nELR: "); uart_put_hex(elr);
    uart_puts("\nRegisters:\n");
    for (int i = 0; i < 31; ++i) {
        uart_puts("  x"); uart_put_dec(i);
        uart_puts(": "); uart_put_hex(regs[i]);
        uart_puts("\n");
    }
}
```

## Common Pitfalls

### Forgetting to Set VBAR_EL1
**Symptom:** Exceptions cause infinite loops or crashes.

**Solution:**
```c
extern void *vectors;
asm volatile("msr vbar_el1, %0" :: "r"(&vectors));
```

### Stack Overflow
**Symptom:** Corruption of kernel data, random crashes.

**Prevention:** Ensure adequate stack size per task (4KB minimum).

### Nested Exceptions Without Re-enabling
**Symptom:** Lost interrupts during exception handling.

**Current:** Interrupts disabled during exception (DAIF masked).

**Future:** Can re-enable after critical section if needed.

### Incorrect Stack Frame Access
**Symptom:** Reading wrong register values in C handler.

**Solution:** Ensure C handler matches assembly layout:
```c
struct exception_frame {
    uint64_t x[31];  // x0-x30
    uint64_t elr;
    uint64_t spsr;
};
```

### Not Restoring Registers
**Symptom:** Corrupted state after exception return.

**Prevention:** Always pair `kernel_entry` with `kernel_exit`.

## Summary

The exception vectors in `vectors.S` provide the critical low-level foundation for all exception handling in MYRASPOS:

1. **16 exception entry points** covering all ARM64 exception scenarios
2. **Complete context save/restore** (272 bytes per exception)
3. **Unified dispatch** to C handlers with full exception information
4. **Minimal overhead** (~70-100 cycles per exception round-trip)
5. **Future-proof design** supporting userspace and SMP extensions

The assembly code is carefully structured to be efficient, maintainable, and compatible with the ARM64 architecture's requirements, forming the bedrock of the operating system's reliability.
