# panic.c/h - Panic Handler and Exception Processing

## Overview

`panic.c` and `panic.h` implement the kernel panic handler and central exception processing for the MYRASPOS kernel. When unrecoverable errors occur or hardware exceptions are raised, this code provides debugging information and safely halts the system.

**Purpose:**
- Handle fatal kernel errors (panic situations)
- Process hardware exceptions (page faults, undefined instructions, etc.)
- Provide stack traces for debugging
- Route system calls to the syscall handler
- Handle IRQ interrupts
- Display diagnostic information before halting

**Key Components:**
1. `panic_with_trace()` - Display error message and stack backtrace
2. `exception_c_handler()` - Central exception dispatcher from assembly vectors
3. Stack unwinding for crash diagnostics

## Data Structures

### `struct pt_regs` (from irq.h)

**Definition:**
```c
struct pt_regs {
    uint64_t regs[30];  /* x0-x29 */
    uint64_t lr;        /* x30 - Link Register */
    uint64_t elr;       /* Exception Link Register (return PC) */
    uint64_t spsr;      /* Saved Program Status Register */
};
```

**Purpose:**
- Captures complete CPU state at moment of exception
- Pushed onto stack by `kernel_entry` macro in vectors.S
- Allows exception handler to inspect/modify task state
- Used for system call argument passing and return values

**Layout in Memory (stack):**
```
High Address
┌──────────────┐
│ spsr         │ +264 bytes
├──────────────┤
│ elr (PC)     │ +256 bytes
├──────────────┤
│ x30 (LR)     │ +248 bytes
├──────────────┤
│ x29          │ +240 bytes
├──────────────┤
│ ...          │
├──────────────┤
│ x1           │ +8 bytes
├──────────────┤
│ x0           │ +0 bytes ← pt_regs pointer
└──────────────┘
Low Address (SP after kernel_entry)
```

### Exception Syndrome Register (ESR_EL1)

**Structure:**
```
 31    26 25 24    0
┌────────┬──┬───────┐
│   EC   │IL│  ISS  │
└────────┴──┴───────┘
```

**Fields:**
- **EC [31:26]** - Exception Class (type of exception)
- **IL [25]** - Instruction Length (0=16-bit, 1=32-bit)
- **ISS [24:0]** - Instruction Specific Syndrome (varies by EC)

**Common Exception Classes:**
```c
EC Value | Exception Type
---------|--------------------------------------
0x00     | Unknown reason
0x01     | Trapped WFI/WFE
0x03     | Trapped MCR/MRC (CP15)
0x15     | SVC (Supervisor Call) from AArch64
0x20     | Instruction Abort from lower EL
0x21     | Instruction Abort from same EL
0x24     | Data Abort from lower EL
0x25     | Data Abort from same EL
0x30     | Breakpoint from lower EL
0x31     | Breakpoint from same EL
```

### Exception Types (type parameter)

The `type` parameter passed to `exception_c_handler()` indicates the exception vector:

```c
Type | Vector              | Description
-----|---------------------|----------------------------
0    | el1_sync_invalid    | Synchronous exception (kernel mode)
1    | el1_irq             | IRQ interrupt (kernel mode)
2    | el1_fiq             | FIQ interrupt (kernel mode)
3    | el1_error           | SError (kernel mode)
4    | el0_sync            | Synchronous exception (user mode)
5    | el0_irq             | IRQ interrupt (user mode)
6    | el0_fiq             | FIQ interrupt (user mode)
7    | el0_error           | SError (user mode)
```

## Key Functions

### `void panic_with_trace(const char *msg)`

**Description:** Display panic message, unwind stack to show call trace, and halt system.

**Prototype:**
```c
void panic_with_trace(const char *msg);
```

**Parameters:**
- `msg` - Error message describing the panic condition

**Returns:** Never returns (infinite loop)

**Implementation:**

```c
void panic_with_trace(const char *msg) {
    uart_puts("\n[PANIC] ");
    uart_puts(msg);
    uart_puts("\nBacktrace:\n");

    // Walk frame pointers
    void **frame = (void **)__builtin_frame_address(0);
    for (int i = 0; i < 16 && frame; ++i) {
        void *ret = *(frame + 1);
        if (!ret) break;
        uart_puts("  ");
        print_hex((uintptr_t)ret);
        uart_puts("\n");
        frame = (void **)(*frame);
    }

    uart_puts("System halted.\n");
    
    uart_puts("Task: ");
    int tid = task_current_id();
    print_hex((uintptr_t)tid);
    uart_puts("\n");

    while (1) ;
}
```

**Stack Walking Algorithm:**
1. Get current frame pointer using `__builtin_frame_address(0)`
2. Follow frame pointer chain:
   - `frame[0]` = Previous frame pointer
   - `frame[1]` = Return address (LR saved by caller)
3. Print each return address (shows call chain)
4. Stop after 16 frames or when frame pointer is NULL

**Stack Frame Structure (ARM64 with frame pointers):**
```
┌──────────────────┐
│ Local variables  │
├──────────────────┤
│ Saved LR (x30)   │ ← frame[1]
├──────────────────┤
│ Saved FP (x29)   │ ← frame[0]
├──────────────────┤ ← FP points here
│ Arguments        │
└──────────────────┘
```

**Example Output:**
```
[PANIC] Division by zero
Backtrace:
  0x0000000040001234
  0x0000000040001456
  0x0000000040001789
  0x00000000400019AB
  0x0000000040001CDE
System halted.
Task: 0x0000000000000003
```

**Limitations:**
- Requires frame pointers (compile with `-fno-omit-frame-pointer`)
- Can't unwind through assembly code without frame setup
- Limited to 16 frames
- No symbol resolution (addresses only)

**Usage:**
```c
if (critical_error) {
    panic_with_trace("Critical error detected");
}
```

### `void exception_c_handler(int type, uint64_t esr, uint64_t elr, struct pt_regs *regs)`

**Description:** Central C exception handler called from assembly exception vectors.

**Prototype:**
```c
void exception_c_handler(int type, uint64_t esr, uint64_t elr, struct pt_regs *regs);
```

**Parameters:**
- `type` - Exception vector type (0-15)
- `esr` - Exception Syndrome Register value
- `elr` - Exception Link Register (return PC)
- `regs` - Pointer to saved register state on stack

**Returns:** Normally returns to continue execution, or panics for fatal errors

**Call Path:**
```
Exception occurs
  ↓
vectors.S: kernel_entry macro
  ├─ Save all registers → pt_regs on stack
  ├─ Load exception info
  ↓
handle_c:
  └─> exception_c_handler(type, esr, elr, regs)
       ↓
      Process exception
       ↓
      Return or panic
       ↓
vectors.S: kernel_exit macro
  └─ Restore registers, eret
```

**Implementation Flow:**

#### 1. IRQ Handling
```c
if (type == 1 || type == 5 || type == 9 || type == 13) {
    irq_entry_c();
    return;
}
```

**IRQ Types:**
- Type 1: IRQ from EL1 (kernel mode)
- Type 5: IRQ from EL0 (user mode)
- Types 9, 13: FIQ variants

**Handling:**
- Call `irq_entry_c()` to dispatch interrupt
- `irq_entry_c()` may call `scheduler_request_preempt()` for timer ticks
- Return to exception handler, which returns to interrupted code

#### 2. System Call Handling
```c
uint32_t ec = (esr >> 26);

if (ec == 0x15) { /* SVC from AArch64 */
    uint32_t sys_num = (uint32_t)regs->regs[8];

    /* Call syscall handler with up to 3 args */
    regs->regs[0] = syscall_handle(sys_num, regs->regs[0], regs->regs[1], regs->regs[2]);
    
    /* Advance PC past SVC instruction */
    regs->elr += 4;
    
    return;
}
```

**System Call Convention:**
- `x8` - Syscall number
- `x0` - Argument 0 / Return value
- `x1` - Argument 1
- `x2` - Argument 2
- EC 0x15 (21 decimal) - SVC instruction exception

**Processing:**
1. Extract syscall number from x8 (`regs->regs[8]`)
2. Call kernel syscall handler with arguments from x0, x1, x2
3. Store return value in x0 (`regs->regs[0]`)
4. Increment PC by 4 to skip `svc #0` instruction
5. Return (exception return will restore modified registers)

**Example System Call:**
```asm
; User code
mov x8, #1      ; SYS_PUTS
mov x0, str_ptr ; Argument: string pointer
svc #0          ; Trigger exception
; Returns here with x0 = return value
```

#### 3. Fatal Exception Handling
```c
uart_puts("\n[PANIC] EXCEPTION OCCURRED!\n");
uart_puts("Type: "); print_hex((uintptr_t)type); uart_puts("\n");
uart_puts("ESR:  "); print_hex((uintptr_t)esr);  uart_puts("\n");
uart_puts("ELR:  "); print_hex((uintptr_t)elr);  uart_puts("\n");

uint64_t far;
asm("mrs %0, far_el1" : "=r"(far));
uart_puts("FAR:  "); print_hex((uintptr_t)far);  uart_puts("\n");

panic_with_trace("Exception");
```

**Diagnostic Information:**
- **Type** - Exception vector (see table above)
- **ESR** - Exception syndrome (cause details)
- **ELR** - Faulting instruction address
- **FAR** - Fault Address Register (for memory exceptions)

**Example Output:**
```
[PANIC] EXCEPTION OCCURRED!
Type: 0x0000000000000004
ESR:  0x0000000092000046
ELR:  0x0000000040001234
FAR:  0x0000000000000000
Backtrace:
  0x0000000040001234
  ...
System halted.
```

### `static void print_hex(uintptr_t x)`

**Description:** Format and print a hexadecimal number to UART.

**Implementation:**
```c
static void print_hex(uintptr_t x) {
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    int pos = 0;
    buf[pos++] = '0'; buf[pos++] = 'x';
    for (int i = (int)(sizeof(uintptr_t) * 2 - 1); i >= 0; --i) {
        int v = (x >> (i * 4)) & 0xF;
        buf[pos++] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
    }
    
    buf[pos] = '\0';
    uart_puts(buf);
}
```

**Output Format:**
- 64-bit value: `0x0123456789ABCDEF` (18 characters)
- 32-bit value: `0x01234567` (10 characters)

**Usage:** Internal helper for panic messages and diagnostics.

## Implementation Details

### Frame Pointer Chain Walking

**Compiler Requirements:**
- Must compile with `-fno-omit-frame-pointer`
- Ensures x29 (FP) forms linked list of stack frames
- Without flag, FP may be used as general-purpose register

**Stack Frame Layout:**
```
Function call: A() → B() → C()

C's Stack:
┌──────────────┐
│ Local vars   │
├──────────────┤
│ LR (ret→B)   │ ← frame[1]
├──────────────┤
│ FP (→B frame)│ ← frame[0]
└──────────────┘ ← x29 (current FP)

B's Stack:
┌──────────────┐
│ Local vars   │
├──────────────┤
│ LR (ret→A)   │ ← frame[1]
├──────────────┤
│ FP (→A frame)│ ← frame[0]
└──────────────┘ ← Pointed by C's frame[0]

A's Stack:
┌──────────────┐
│ Local vars   │
├──────────────┤
│ LR (ret→...)  │
├──────────────┤
│ FP (→..frame)│
└──────────────┘
```

Walking this chain prints: `[ret→B, ret→A, ...]`

### Exception Syndrome Decoding

**Extract Exception Class:**
```c
uint32_t ec = (esr >> 26);
```

**Common EC Values:**
- `0x00` - Unknown/uncategorized
- `0x15` - SVC (System Call)
- `0x20` - Instruction abort (lower EL)
- `0x21` - Instruction abort (same EL)
- `0x24` - Data abort (lower EL)
- `0x25` - Data abort (same EL)

**ISS Field (varies by EC):**
For data aborts (EC 0x24/0x25):
```
ISS bits:
[5:0]   - DFSC (Data Fault Status Code)
[6]     - WnR (Write not Read)
[7]     - S1PTW (stage 1 translation)
[8]     - CM (Cache Maintenance)
[9]     - EA (External Abort)
[10]    - FnV (FAR not Valid)
[23:14] - Reserved
```

### System Call PC Advancement

**Why increment elr by 4?**
```c
regs->elr += 4;
```

**ARM64 instruction size:**
- `svc #0` is a 32-bit instruction (4 bytes)
- After executing syscall, must return to next instruction
- Without increment, would re-execute `svc #0` infinitely

**User code sequence:**
```asm
0x1000: mov x8, #1      ; Setup syscall
0x1004: svc #0          ; Trigger exception (ELR = 0x1004)
0x1008: mov x1, x0      ; Use return value
```

After syscall, ELR = 0x1008, so exception return continues at next instruction.

### Register State Modification

**System calls modify register state:**
```c
regs->regs[0] = syscall_handle(...);
```

When exception returns:
1. `kernel_exit` restores all registers from `pt_regs`
2. x0 now contains syscall return value
3. Task sees modified x0

This is how system calls return values to callers.

### IRQ Preemption Notes

**Current implementation:**
```c
if (type == IRQ) {
    irq_entry_c();
    return;
}
```

`irq_entry_c()` may call `scheduler_request_preempt()`, but actual preemption doesn't occur here. The comment in code notes this limitation:

```c
/* Preemption logic usually requires checking 'need_resched' 
   before restoring regs. That is separate issue. */
```

**Ideal implementation:**
- Before `kernel_exit`, check if reschedule needed
- If yes, call `schedule()` which switches tasks
- New task's context loaded instead of original

**Current behavior:**
- IRQ handled, flag set
- Return to interrupted task
- Preemption happens at next timer tick or yield

## Design Decisions

### Why Separate IRQ and Exception Handling?

**Different purposes:**
- **IRQ** - Normal hardware interrupts (timer, devices)
- **Exceptions** - Error conditions or syscalls

**Different handling:**
- IRQ: Dispatch to handler, potentially preempt
- Exception: Often fatal, requires diagnostics

### Why Print Before Halting?

**Debugging necessity:**
- No debugger available in early boot
- UART is most reliable output mechanism
- Stack trace essential for diagnosing crashes

**Information priorities:**
1. Error message (what went wrong)
2. Register state (where it happened)
3. Stack trace (how we got there)
4. Current task ID (who was running)

### Why Only 16 Stack Frames?

**Practical limits:**
- Prevents infinite loops on corrupted stacks
- Provides enough context for most debugging
- Keeps panic output manageable
- Typical call depth is < 10 frames

### Why Not Symbol Resolution?

**Complexity vs. Benefit:**
- Requires symbol table in kernel
- Significant memory overhead
- Offline tools (addr2line, objdump) can resolve addresses
- Simplified kernel implementation

**Workflow:**
```bash
# Get panic address
[PANIC] 0x0000000040001234

# Resolve offline
$ addr2line -e kernel.elf 0x40001234
kernel/sched.c:123

$ aarch64-none-elf-objdump -d kernel.elf | grep 40001234
40001234:  d503201f  nop
```

### Why Modify ELR for Syscalls?

**Instruction replay prevention:**
- SVC instruction must not be re-executed
- User expects syscall to complete once
- PC must advance to next instruction

**Exception:** Page faults should NOT advance PC:
- Page fault handler fixes mapping
- Returns to faulting instruction
- Instruction re-executed successfully

Current implementation doesn't handle page faults (panics instead).

## Usage Examples

### Triggering a Panic

**From kernel code:**
```c
if (kmalloc(size) == NULL) {
    panic_with_trace("Out of memory");
}
```

**Output:**
```
[PANIC] Out of memory
Backtrace:
  0x0000000040002468  ; kmalloc+0x20
  0x0000000040003ABC  ; my_function+0x1C
  0x0000000040004DEF  ; caller+0x8
System halted.
Task: 0x0000000000000001
```

### Making a System Call from User Task

**User task code:**
```c
void user_task(void) {
    // Call SYS_PUTS (syscall #1)
    asm volatile(
        "mov x8, #1\n"       // Syscall number
        "mov x0, %0\n"       // Argument: string pointer
        "svc #0\n"           // Trigger exception
        :: "r"("Hello\n")
        : "x0", "x8"
    );
}
```

**Exception flow:**
1. `svc #0` triggers exception
2. CPU vectors to `el0_sync` in vectors.S
3. `kernel_entry` saves registers
4. Calls `exception_c_handler(type=4, esr=0x56000001, ...)`
5. Handler extracts EC=0x15 (SVC)
6. Calls `syscall_handle(1, "Hello\n", 0, 0)`
7. Syscall handler prints string
8. Returns, handler increments elr
9. `kernel_exit` restores registers
10. `eret` returns to user task (PC now past `svc`)

### Handling Page Fault (Not Implemented)

**Hypothetical implementation:**
```c
uint32_t ec = (esr >> 26);

if (ec == 0x24 || ec == 0x25) {  // Data abort
    uint64_t far;
    asm("mrs %0, far_el1" : "=r"(far));
    
    // Try to handle page fault
    if (handle_page_fault(far, esr, regs)) {
        // Success - fault resolved, return to retry
        return;
    }
    
    // Couldn't handle - panic
    uart_puts("Unhandled page fault at: ");
    print_hex(far);
    panic_with_trace("Page fault");
}
```

**Note:** Current code doesn't handle page faults, it panics.

### Exception with Register Dump

**Add register dumping to panic:**
```c
void dump_registers(struct pt_regs *regs) {
    uart_puts("Registers:\n");
    for (int i = 0; i < 30; i++) {
        uart_puts(" x"); uart_put_dec(i); uart_puts(": ");
        print_hex(regs->regs[i]);
        uart_puts("\n");
    }
    uart_puts(" LR: "); print_hex(regs->lr); uart_puts("\n");
    uart_puts(" ELR: "); print_hex(regs->elr); uart_puts("\n");
    uart_puts(" SPSR: "); print_hex(regs->spsr); uart_puts("\n");
}

void exception_c_handler(...) {
    // ... existing code ...
    
    dump_registers(regs);
    panic_with_trace("Exception");
}
```

## Cross-References

### Related Documentation
- [kernel.md](kernel.md) - Kernel initialization
- [init.md](init.md) - Init task and syscall wrappers
- [swtch.md](swtch.md) - Context switching
- [../arch/vectors.md](../arch/vectors.md) - Exception vectors assembly
- [../arch/arm64.md](../arch/arm64.md) - ARM64 architecture details
- [../syscall/syscall.md](../syscall/syscall.md) - System call implementation
- [../irq/irq.md](../irq/irq.md) - Interrupt handling

### Source Code Dependencies

**Includes:**
```c
#include "panic.h"     // Function declarations
#include "uart.h"      // Console output
#include <stdint.h>    // Fixed-width types
#include <stddef.h>    // size_t, NULL
#include "sched.h"     // task_current_id()
#include "irq.h"       // struct pt_regs, irq_entry_c()
#include "syscall.h"   // syscall_handle()
```

**Exports:**
```c
void panic_with_trace(const char *msg);
void exception_c_handler(int type, uint64_t esr, uint64_t elr, struct pt_regs *regs);
```

### Exception Flow Diagram

```
Hardware Exception Occurs
  │
  ↓
CPU Exception Logic
  ├─ Save PC → ELR_EL1
  ├─ Save PSTATE → SPSR_EL1
  ├─ Set PC to VBAR_EL1 + vector_offset
  ↓
vectors.S (exception vector)
  │
  ├─ Determine exception type
  ↓
kernel_entry macro
  ├─ Save x0-x30 to stack
  ├─ Save ELR, SPSR to stack
  ├─ Form pt_regs structure on stack
  ↓
handle_c:
  ├─ Load type, ESR, ELR, pt_regs pointer
  └─> Call exception_c_handler(type, esr, elr, regs)
       │
       ├─ Is IRQ? → irq_entry_c() → return
       ├─ Is SVC? → syscall_handle() → return
       └─ Else → panic_with_trace()
            │
            └─ Print diagnostics
            └─ Halt (infinite loop)
       │
  ┌────┘
  ↓
kernel_exit macro
  ├─ Restore x0-x30 from stack
  ├─ Restore ELR, SPSR
  └─ eret (exception return)
       │
       └─> Resume execution at ELR
```

### System Call Flow

```
User Task
  │
  └─> mov x8, #1; svc #0
       │
       └─> Exception: EL0 Sync
            │
            └─> vectors.S:el0_sync
                 │
                 └─> kernel_entry (save all regs)
                      │
                      └─> exception_c_handler(type=4, esr=..., ...)
                           │
                           ├─ Extract EC = 0x15 (SVC)
                           ├─ Get syscall# from regs->regs[8]
                           ├─ Get args from regs->regs[0..2]
                           │
                           ├─> syscall_handle(sys#, arg0, arg1, arg2)
                           │    │
                           │    └─> Kernel syscall implementation
                           │         └─> Returns result
                           │
                           ├─ Store result in regs->regs[0]
                           ├─ Increment regs->elr by 4
                           └─ Return
                                │
                                └─> kernel_exit (restore regs with new x0)
                                     │
                                     └─> eret → User task continues
```

### Preemption Flow (Current Limitation)

```
Timer IRQ fires during task execution
  │
  └─> vectors.S:el0_irq or el1_irq
       │
       └─> kernel_entry (save regs)
            │
            └─> exception_c_handler(type=5 or 1, ...)
                 │
                 └─> irq_entry_c()
                      │
                      ├─ Dispatch timer interrupt
                      ├─ scheduler_request_preempt() sets flag
                      └─ Return
                           │
                           └─> Return from exception_c_handler
                                │
                                └─> kernel_exit
                                     │
                                     └─> eret → Same task resumes
                                     
[Note: Preemption doesn't happen here!
 It happens later at next yield() or timer tick
 when schedule() is explicitly called]

Ideal Flow (Not Implemented):
  kernel_exit:
    ├─ Check if preempt requested
    ├─ If yes: call schedule()
    │   └─> Context switch to new task
    └─ Else: eret to same task
```

This exception handling code is critical for debugging and provides the foundation for syscalls and interrupt handling in MYRASPOS.
