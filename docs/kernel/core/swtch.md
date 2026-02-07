# swtch.S - Context Switching Assembly

## Overview

`swtch.S` contains the low-level assembly code for context switching between tasks on ARM64 (AArch64) architecture. This file implements the core mechanism that allows the scheduler to save the current task's execution state and restore another task's state, enabling cooperative and preemptive multitasking.

**Purpose:**
- Save complete CPU register state (callee-saved registers)
- Switch stack pointers between tasks
- Restore register state for next task
- Handle first-time task execution via `ret_from_fork`

**Key Components:**
1. `cpu_switch_to` - Main context switch routine
2. `ret_from_fork` - Entry point for newly created tasks

## Data Structures

### `struct task_context` (from sched.h)

The context structure stores the CPU state that must be preserved across context switches:

```c
struct task_context {
    uint64_t x19;   // Callee-saved register
    uint64_t x20;   // Callee-saved register (also used for arg in ret_from_fork)
    uint64_t x21;   // Callee-saved register
    uint64_t x22;   // Callee-saved register
    uint64_t x23;   // Callee-saved register
    uint64_t x24;   // Callee-saved register
    uint64_t x25;   // Callee-saved register
    uint64_t x26;   // Callee-saved register
    uint64_t x27;   // Callee-saved register
    uint64_t x28;   // Callee-saved register
    uint64_t x29;   // Frame Pointer (FP)
    uint64_t x30;   // Link Register (LR) - return address
    uint64_t sp;    // Stack Pointer
};
```

**Size:** 13 × 8 bytes = 104 bytes

**Memory Layout:**
```
Offset  | Register | Purpose
--------|----------|----------------------------------
+0      | x19      | General purpose (callee-saved)
+8      | x20      | General purpose / fork arg
+16     | x21      | General purpose (callee-saved)
+24     | x22      | General purpose (callee-saved)
+32     | x23      | General purpose (callee-saved)
+40     | x24      | General purpose (callee-saved)
+48     | x25      | General purpose (callee-saved)
+56     | x26      | General purpose (callee-saved)
+64     | x27      | General purpose (callee-saved)
+72     | x28      | General purpose (callee-saved)
+80     | x29      | Frame Pointer
+88     | x30      | Return Address
+96     | sp       | Stack Pointer
```

### ARM64 Register Conventions

**Callee-saved registers (must be preserved):**
- x19-x28: General purpose
- x29: Frame pointer (FP)
- x30: Link register (LR)
- sp: Stack pointer

**Caller-saved registers (not saved in context):**
- x0-x18: Temporary/argument/return value registers
- x29 can also be used as general purpose in some contexts

**Why only callee-saved?**
The caller-saved registers (x0-x18) are already saved by the C compiler before calling `cpu_switch_to()`, so we only need to preserve the registers that the compiler expects to remain unchanged across function calls.

## Key Functions

### `cpu_switch_to(struct task_context *prev, struct task_context *next)`

**Description:** Switch from current task to next task by saving/restoring register context.

**Assembly Signature:**
```asm
.global cpu_switch_to
cpu_switch_to:
```

**C Prototype:**
```c
void cpu_switch_to(struct task_context *prev, struct task_context *next);
```

**Parameters:**
- `x0` - Pointer to previous task's context (save location)
- `x1` - Pointer to next task's context (load location)

**Returns:** 
- Appears to return normally, but actually returns in the context of the next task
- The "previous" task will resume here when it's scheduled again

**Detailed Assembly Implementation:**

#### Phase 1: Save Previous Task Context
```asm
cpu_switch_to:
    /* Save current context to prev (x0 points to prev context) */
    stp x19, x20, [x0, #0]      /* Store pair: x19,x20 at offset 0 */
    stp x21, x22, [x0, #16]     /* Store pair: x21,x22 at offset 16 */
    stp x23, x24, [x0, #32]     /* Store pair: x23,x24 at offset 32 */
    stp x25, x26, [x0, #48]     /* Store pair: x25,x26 at offset 48 */
    stp x27, x28, [x0, #64]     /* Store pair: x27,x28 at offset 64 */
    stp x29, x30, [x0, #80]     /* Store FP and LR at offset 80 */
    
    mov x2, sp                   /* Copy stack pointer to temp register */
    str x2, [x0, #96]           /* Store SP at offset 96 */
```

**Explanation:**
- `stp` (Store Pair) - Stores two 64-bit registers to consecutive memory locations
- More efficient than individual stores (single instruction for two registers)
- Current SP must be moved to x2 first (can't store sp directly)

#### Phase 2: Load Next Task Context
```asm
    /* Load next context (x1 points to next context) */
    ldp x19, x20, [x1, #0]      /* Load pair: x19,x20 from offset 0 */
    ldp x21, x22, [x1, #16]     /* Load pair: x21,x22 from offset 16 */
    ldp x23, x24, [x1, #32]     /* Load pair: x23,x24 from offset 32 */
    ldp x25, x26, [x1, #48]     /* Load pair: x25,x26 from offset 48 */
    ldp x27, x28, [x1, #64]     /* Load pair: x27,x28 from offset 64 */
    ldp x29, x30, [x1, #80]     /* Load FP and LR from offset 80 */
    
    ldr x2, [x1, #96]           /* Load SP from offset 96 */
    mov sp, x2                   /* Restore stack pointer */

    ret                          /* Return to address in x30 (LR) */
```

**Explanation:**
- `ldp` (Load Pair) - Loads two 64-bit registers from consecutive memory locations
- SP must be loaded to temp register first, then moved to sp
- `ret` branches to address in x30, which was just loaded from next task's context

**Critical Observation:**
The `ret` instruction doesn't return to the caller of `cpu_switch_to()`. Instead:
- For running tasks: Returns to where that task was previously switched out
- For new tasks: Returns to `ret_from_fork` (set during task creation)

### `ret_from_fork`

**Description:** Entry point for newly created tasks. Sets up environment and calls the task function.

**Assembly Signature:**
```asm
.global ret_from_fork
ret_from_fork:
```

**Purpose:**
- Enable interrupts for new task
- Call task function with proper argument
- Handle task exit if function returns

**Detailed Assembly Implementation:**

```asm
ret_from_fork:
    /* Enable IRQs for new task */
    msr daifclr, #2
```

**Interrupt Enable:**
- `msr daifclr, #2` - Clear IRQ mask bit (enable interrupts)
- `daif` register controls interrupt masking:
  - D - Debug exceptions
  - A - SError (async abort)
  - I - IRQ (bit 1)
  - F - FIQ (fast interrupt)
- Clearing bit 1 enables IRQ interrupts

**Why enable interrupts here?**
- New tasks inherit disabled interrupts from task creation
- Must be enabled so timer can preempt the task
- Done here rather than in scheduler to avoid race conditions

```asm
    /* Call task function directly - no debug overhead */
    mov x0, x20    /* Argument to x0 */
    blr x19        /* Call task function */
```

**Task Function Call:**
- `x19` contains function pointer (set during task creation)
- `x20` contains function argument (set during task creation)
- `mov x0, x20` - Move argument to x0 (first parameter register)
- `blr x19` - Branch with Link to address in x19
  - `blr` (Branch Link Register) calls the function
  - Return address is saved in x30 (LR)

**Register Setup During Task Creation:**
```c
// In scheduler (sched.c):
context->x19 = (uint64_t)fn;           // Function pointer
context->x20 = (uint64_t)arg;          // Function argument
context->x30 = (uint64_t)ret_from_fork; // Return address
context->sp = (uint64_t)stack_top;     // Stack pointer
```

```asm
    /* If task returns, kill it */
    mov x0, #0
    bl task_exit   /* Call task_exit (needs to be implemented) */
    
    /* Should not reach here */
hang:
    b hang
```

**Task Exit Handling:**
- If task function returns (shouldn't happen for most tasks)
- Call `task_exit(0)` to cleanly terminate the task
- If `task_exit` returns (shouldn't happen), infinite loop to prevent corruption

**Note:** `task_exit` must be implemented in the kernel and exported for this to work properly.

## Assembly Details

### Register Save/Restore Sequence

**Why use Store/Load Pair instructions?**
- **Performance:** One instruction instead of two
- **Atomicity:** Both registers stored/loaded atomically
- **Memory Efficiency:** Better cache utilization

**Example:**
```asm
stp x19, x20, [x0, #0]
```
Equivalent to:
```asm
str x19, [x0, #0]
str x20, [x0, #8]
```
But faster and more cache-friendly.

### Stack Switching

**Critical section:**
```asm
mov x2, sp          /* Copy current SP */
str x2, [x0, #96]   /* Save it */
ldr x2, [x1, #96]   /* Load next SP */
mov sp, x2          /* Switch to next stack */
```

**Why use a temporary register?**
ARM64 architecture doesn't allow direct store/load of `sp` in some contexts. Using `x2` as intermediary ensures portability and correctness.

**Stack Layout After Switch:**
```
Old Task Stack          New Task Stack
┌─────────────┐        ┌─────────────┐
│   Frame 3   │        │   Frame 3   │
├─────────────┤        ├─────────────┤
│   Frame 2   │        │   Frame 2   │
├─────────────┤        ├─────────────┤
│   Frame 1   │   →    │   Frame 1   │ ← sp (now)
├─────────────┤        ├─────────────┤
│     ...     │        │     ...     │
└─────────────┘        └─────────────┘
```

### Return Mechanism

**Normal task switch:**
```asm
ret    /* Branch to address in x30 */
```

When task A switches to task B:
1. Task A's x30 points back into A's code (wherever it called schedule())
2. cpu_switch_to saves A's x30 to A's context
3. cpu_switch_to loads B's x30 from B's context
4. `ret` branches to B's x30, resuming B

**First-time task execution:**
1. During creation, task's x30 is set to `ret_from_fork`
2. First context switch loads x30 = ret_from_fork
3. `ret` branches to `ret_from_fork`
4. `ret_from_fork` enables interrupts and calls task function

### Interrupt State During Context Switch

**Interrupts during `cpu_switch_to`:**
- Must be disabled during context switch
- Prevents preemption while SP is inconsistent
- Caller (scheduler) disables interrupts before calling
- Re-enabled by `ret_from_fork` for new tasks
- Existing tasks have interrupts in their saved PSTATE

## Implementation Details

### Why Only Callee-Saved Registers?

**ARM64 Calling Convention (AAPCS64):**

**Callee-saved (preserved across function calls):**
- x19-x28: Must be restored by callee if modified
- x29 (FP): Frame pointer
- x30 (LR): Return address
- SP: Stack pointer

**Caller-saved (not preserved):**
- x0-x7: Argument passing and return values
- x8-x18: Temporary registers
- x29 can be used as general purpose if not using frame pointers

**Why this works:**
When task A calls `schedule()` which calls `cpu_switch_to()`:
1. Compiler saves x0-x18 on task A's stack (if needed)
2. `cpu_switch_to()` saves x19-x30 and SP to A's context
3. When A resumes later, x19-x30 and SP are restored
4. Return from `cpu_switch_to()` back through `schedule()`
5. Compiler restores x0-x18 from stack

### Context Structure Alignment

**Natural alignment:**
All fields are 8-byte aligned (uint64_t):
- Offset 0: 8-byte aligned (x19)
- Offset 8: 8-byte aligned (x20)
- ...
- Offset 96: 8-byte aligned (sp)

**Why alignment matters:**
- ARM64 requires aligned memory access for performance
- Unaligned access causes exceptions or slowdowns
- stp/ldp require 8-byte alignment

### Task Creation Context Setup

**Scheduler prepares context for new task:**
```c
// In sched.c
struct task_context *ctx = &task->context;
ctx->x19 = (uint64_t)fn;            // Task function
ctx->x20 = (uint64_t)arg;           // Task argument
ctx->x30 = (uint64_t)ret_from_fork; // First "return" goes here
ctx->sp = (uint64_t)stack_top;      // Top of task's stack
// x21-x29 don't matter for new task (will be overwritten)
```

When this task is first scheduled:
1. `cpu_switch_to()` loads all registers from context
2. `ret` branches to x30 = `ret_from_fork`
3. `ret_from_fork` calls x19(x20) = fn(arg)

## Design Decisions

### Why Assembly Instead of C?

**Fundamental requirement:**
Context switching requires:
- Direct register manipulation
- Stack pointer modification
- Precise control over instruction sequencing

C cannot express these operations reliably:
- Compiler may clobber registers unpredictably
- Stack switching in C is undefined behavior
- No way to guarantee instruction order

### Why Store SP Separately?

**Stack pointer is special:**
- Can't be directly stored/loaded with `stp`/`ldp`
- Must go through temporary register
- Represents entire stack context

Keeping SP as last field makes it easy to:
- Access independently
- Verify structure size
- Debug stack issues

### Why Enable Interrupts in ret_from_fork?

**Timing considerations:**
1. **Too early** - Before task fully set up, interrupts could corrupt state
2. **Too late** - Task might run for too long without preemption
3. **Just right** - In `ret_from_fork`, task is ready to run but hasn't started

**Per-task enablement:**
Each task enables its own interrupts on first run. This ensures:
- No race conditions during task creation
- Consistent interrupt state for all tasks
- Simpler scheduler logic

### Why Not Save All Registers?

**Performance trade-off:**
- Saving x0-x18: 19 × 8 = 152 extra bytes per task
- Load/store time: ~38 instructions extra
- Cache pressure: Larger context structures

**Correctness guaranteed by calling convention:**
- Caller already saved x0-x18 if needed
- Callee (cpu_switch_to) must preserve x19-x28
- No registers are lost

## Usage Examples

### Scheduler Context Switch

**Typical invocation from scheduler:**
```c
void schedule(void) {
    // Disable interrupts
    unsigned long flags = irq_save();
    
    struct task *prev = current_task;
    struct task *next = pick_next_task();
    
    if (prev != next) {
        current_task = next;
        // Context switch happens here
        cpu_switch_to(&prev->context, &next->context);
        // When this task is scheduled again, execution resumes here
    }
    
    // Restore interrupts
    irq_restore(flags);
}
```

**Flow:**
1. `schedule()` calls `cpu_switch_to()`
2. Prev task's registers saved
3. Next task's registers loaded
4. `ret` continues next task
5. Eventually, prev task is scheduled again
6. `ret` returns to prev task's `schedule()` call
7. Prev task continues executing

### Task Creation

**Setting up a new task:**
```c
struct task *task_create(task_fn fn, void *arg, const char *name) {
    struct task *t = allocate_task_struct();
    void *stack = allocate_stack(STACK_SIZE);
    
    // Initialize context for new task
    t->context.x19 = (uint64_t)fn;              // Function pointer
    t->context.x20 = (uint64_t)arg;             // Argument
    t->context.x30 = (uint64_t)ret_from_fork;   // First return address
    t->context.sp = (uint64_t)(stack + STACK_SIZE); // Stack top
    
    // x21-x29 are undefined for new task (will be set when task runs)
    
    add_to_ready_queue(t);
    return t;
}
```

**First execution:**
```
schedule() → cpu_switch_to() → ret → ret_from_fork → task_fn(arg)
```

### Voluntary Yield

**Task voluntarily yields CPU:**
```c
void yield(void) {
    schedule();  // Triggers context switch
}

void my_task(void *arg) {
    while (1) {
        do_some_work();
        yield();  // Let other tasks run
    }
}
```

**Context switch sequence:**
```
1. my_task calls yield()
2. yield calls schedule()
3. schedule calls cpu_switch_to(&my_task->ctx, &next_task->ctx)
4. Registers saved, next task loaded
5. Next task runs
...
N. Eventually, my_task scheduled again
N+1. cpu_switch_to returns into my_task's schedule()
N+2. schedule() returns to yield()
N+3. yield() returns to my_task
N+4. my_task continues after yield() call
```

### Preemptive Scheduling

**Timer interrupt preempts task:**
```
1. Task running, timer interrupt fires
2. CPU vectors to interrupt handler (vectors.S)
3. Handler saves ALL registers (x0-x30, sp, PC) to pt_regs on task's stack
4. Handler calls irq_entry_c()
5. irq_entry_c() calls scheduler_request_preempt()
6. Return from interrupt (vectors.S)
7. Before restoring registers, check if schedule needed
8. If yes, call schedule() which calls cpu_switch_to()
9. New task selected and switched to
10. Old task's context is in task_context + pt_regs on its stack
```

**Note:** Current implementation doesn't fully check for preemption on IRQ return, but the mechanism is in place.

## Cross-References

### Related Documentation
- [kernel.md](kernel.md) - Kernel initialization
- [init.md](init.md) - Init task execution via ret_from_fork
- [panic.md](panic.md) - Exception handling and debugging
- [../sched/scheduler.md](../sched/scheduler.md) - Scheduler that calls cpu_switch_to
- [../arch/arm64.md](../arch/arm64.md) - ARM64 architecture specifics
- [../arch/registers.md](../arch/registers.md) - ARM64 register conventions
- [../irq/vectors.md](../irq/vectors.md) - Exception vectors and full register save

### Source Code Dependencies

**Includes:**
- None (pure assembly file)

**Exports:**
```asm
.global cpu_switch_to
.global ret_from_fork
```

**Imports:**
```c
extern void task_exit(int code);  // Called if task returns
```

### Data Structure Dependency

**Defined in:** `kernel/sched.h`
```c
struct task_context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;  // FP
    uint64_t x30;  // LR
    uint64_t sp;   // SP
};
```

**Critical:** The order and size of fields in `task_context` must match exactly with the offsets used in assembly:
- x19 at offset 0
- x20 at offset 8
- ...
- sp at offset 96

**Verification:**
```c
// In sched.c or test code
_Static_assert(offsetof(struct task_context, x19) == 0, "x19 offset");
_Static_assert(offsetof(struct task_context, x20) == 8, "x20 offset");
_Static_assert(offsetof(struct task_context, sp) == 96, "sp offset");
```

### Calling Sequence Diagram

```
Task A executing
  │
  ├─> calls schedule() or yield()
  │    │
  │    └─> schedule() in sched.c
  │         │
  │         ├─ Disable interrupts
  │         ├─ Pick next task (Task B)
  │         │
  │         └─> cpu_switch_to(&A->context, &B->context)
  │              │
  │              ├─ [ASSEMBLY] Save A's x19-x30, sp
  │              ├─ [ASSEMBLY] Load B's x19-x30, sp
  │              └─ [ASSEMBLY] ret (branches to B's x30)
  │                   │
  │                   ├─ If B is existing task:
  │                   │  └─> Returns into B's schedule() call
  │                   │       └─> B continues from where it yielded
  │                   │
  │                   └─ If B is new task:
  │                      └─> ret_from_fork
  │                           │
  │                           ├─ Enable interrupts (msr daifclr, #2)
  │                           └─ blr x19 → Task function starts
  │
  └─ [Task A suspended]
      ...
      [Later, Task A scheduled again]
      ...
  │
  └─> cpu_switch_to() "returns" into A's schedule()
       │
       └─> A resumes execution

```

### Register State Flow

```
Task Creation:
┌──────────────────────────────────────────┐
│ task_context initialized:               │
│ x19 = task_fn                            │
│ x20 = arg                                │
│ x30 = ret_from_fork                      │
│ sp = stack_top                           │
└──────────────────────────────────────────┘
                ↓
         First Schedule
                ↓
┌──────────────────────────────────────────┐
│ cpu_switch_to loads:                     │
│ x19 = task_fn                            │
│ x20 = arg                                │
│ x30 = ret_from_fork                      │
│ sp = stack_top                           │
│ ret → branches to ret_from_fork          │
└──────────────────────────────────────────┘
                ↓
         ret_from_fork
                ↓
┌──────────────────────────────────────────┐
│ msr daifclr, #2  (enable IRQ)            │
│ mov x0, x20      (arg → x0)              │
│ blr x19          (call task_fn)          │
└──────────────────────────────────────────┘
                ↓
         Task Executes
```

This assembly code is the heart of multitasking in MYRASPOS, enabling the illusion of concurrent execution through rapid context switching.
