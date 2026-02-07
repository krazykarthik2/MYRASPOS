# MYRASPOS Core Kernel Documentation

## Overview

This directory contains comprehensive technical documentation for the core kernel components of MYRASPOS. These are the fundamental pieces that provide the foundation for the entire operating system: kernel initialization, the init task (PID 1), context switching, and panic/exception handling.

## Documents

### [kernel.md](kernel.md)
**Main Kernel Entry Point and Initialization**

Documents `kernel.c` and `kernel.h` - the central coordination point for kernel startup.

**Key Topics:**
- Boot sequence from bootloader to kernel
- Memory subsystem initialization (palloc, kmalloc, MMU)
- Filesystem initialization (ramfs, diskfs)
- Graphics initialization (virtio-gpu, RAMFB)
- Timer, interrupt, and scheduler setup
- Init task creation
- Main scheduler loop

**Essential for understanding:**
- System boot flow
- Initialization order and dependencies
- Subsystem integration

---

### [init.md](init.md)
**Init Task (PID 1) Implementation**

Documents `init.c` and `init.h` - the first user-level task that completes system initialization.

**Key Topics:**
- Init task lifecycle and responsibilities
- Service system setup (systemd-like units)
- Filesystem operations and synchronization
- GUI subsystem initialization
- Shell task creation
- Syscall wrapper API for user tasks

**Essential for understanding:**
- System service management
- How user-level initialization works
- Syscall interface design
- Task hierarchy and dependencies

---

### [swtch.md](swtch.md)
**Context Switching Assembly**

Documents `swtch.S` - the low-level ARM64 assembly for task context switching.

**Key Topics:**
- `cpu_switch_to()` - main context switch routine
- `ret_from_fork()` - new task entry point
- Register save/restore mechanism
- Stack switching
- ARM64 calling conventions
- Task context structure layout

**Essential for understanding:**
- How multitasking actually works
- Register state management
- Assembly-level task switching
- First-time task execution

---

### [panic.md](panic.md)
**Panic Handler and Exception Processing**

Documents `panic.c` and `panic.h` - error handling and exception processing.

**Key Topics:**
- `panic_with_trace()` - stack unwinding and diagnostics
- `exception_c_handler()` - central exception dispatcher
- Hardware exception processing
- System call handling
- IRQ routing
- Debugging information display

**Essential for understanding:**
- Error handling and debugging
- Exception vector processing
- System call mechanism
- Interrupt handling flow
- Stack trace generation

---

## Component Relationships

### Boot Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    System Boot Sequence                      │
└─────────────────────────────────────────────────────────────┘
                            │
                            ↓
        ┌──────────────────────────────────┐
        │  boot/start.s                    │
        │  - Set EL1                       │
        │  - Setup exception vectors       │
        │  - Initialize stack              │
        │  - Clear BSS                     │
        └───────────┬──────────────────────┘
                    │
                    ↓ bl kernel_main
        ┌──────────────────────────────────┐
        │  kernel.c:kernel_main()          │
        │  - Init memory (palloc, kmalloc) │
        │  - Enable MMU                    │
        │  - Init filesystems              │
        │  - Init graphics                 │
        │  - Init timer/IRQ                │
        │  - Init scheduler                │
        │  - Create init task              │
        └───────────┬──────────────────────┘
                    │
                    ↓ schedule()
        ┌──────────────────────────────────┐
        │  swtch.S:cpu_switch_to()         │
        │  - Save kernel context           │
        │  - Load init context             │
        │  - ret → ret_from_fork           │
        └───────────┬──────────────────────┘
                    │
                    ↓ blr x19
        ┌──────────────────────────────────┐
        │  init.c:init_main()              │
        │  - Create service units          │
        │  - Start services                │
        │  - Init input/VFS/diskfs         │
        │  - Start GUI (wm)                │
        │  - Start shell                   │
        │  - Idle forever                  │
        └──────────────────────────────────┘
```

### Exception Handling Flow

```
┌───────────────────────────────────────────────────────────┐
│              Hardware Exception / Interrupt                │
└────────────────────────┬──────────────────────────────────┘
                         │
                         ↓
        ┌────────────────────────────────┐
        │  vectors.S                     │
        │  - Exception vector table      │
        │  - kernel_entry (save regs)    │
        └────────────┬───────────────────┘
                     │
                     ↓
        ┌────────────────────────────────┐
        │  panic.c:exception_c_handler() │
        │  - Route IRQ → irq_entry_c()   │
        │  - Route SVC → syscall_handle()│
        │  - Fatal errors → panic()      │
        └────────────┬───────────────────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ↓            ↓            ↓
    ┌──────┐   ┌─────────┐  ┌──────────┐
    │ IRQ  │   │ Syscall │  │  Panic   │
    │Handle│   │ Handle  │  │ + Trace  │
    └──┬───┘   └────┬────┘  └─────┬────┘
       │            │             │
       └────────────┴─────────────┘
                    │
                    ↓
        ┌────────────────────────────────┐
        │  vectors.S:kernel_exit         │
        │  - Restore registers           │
        │  - eret (return from exception)│
        └────────────────────────────────┘
```

### Task Context Switching

```
┌────────────────────────────────────────────────────────┐
│                   Task A executing                      │
└───────────────────────┬────────────────────────────────┘
                        │
                        ↓ yield() or preemption
        ┌───────────────────────────────┐
        │  sched.c:schedule()           │
        │  - Pick next task (Task B)    │
        │  - Disable interrupts         │
        └────────────┬──────────────────┘
                     │
                     ↓ cpu_switch_to(&A->ctx, &B->ctx)
        ┌───────────────────────────────┐
        │  swtch.S:cpu_switch_to        │
        │  Phase 1: Save Task A context │
        │    - x19-x30, sp → A->context │
        │                               │
        │  Phase 2: Load Task B context │
        │    - x19-x30, sp ← B->context │
        │                               │
        │  - ret (to B's x30)           │
        └────────────┬──────────────────┘
                     │
        ┌────────────┴─────────────┐
        │                          │
        ↓ (existing task)          ↓ (new task)
┌────────────────────┐    ┌────────────────────┐
│ Return to B's      │    │ swtch.S:           │
│ schedule() call    │    │ ret_from_fork      │
│                    │    │ - Enable IRQs      │
│ B resumes where    │    │ - blr x19 → fn(arg)│
│ it yielded         │    │                    │
└────────────────────┘    └─────────┬──────────┘
                                    │
                                    ↓
                        ┌────────────────────┐
                        │ Task B executes    │
                        │ (first time)       │
                        └────────────────────┘
```

### System Call Flow

```
┌──────────────────────────────────────────────────────────┐
│              User Task Executes svc #0                    │
└────────────────────────┬─────────────────────────────────┘
                         │
                         ↓ Hardware exception
        ┌────────────────────────────────┐
        │  vectors.S:el0_sync            │
        │  - kernel_entry (save all regs)│
        └────────────┬───────────────────┘
                     │
                     ↓
        ┌────────────────────────────────┐
        │  panic.c:exception_c_handler() │
        │  - Extract EC = 0x15 (SVC)     │
        │  - Get syscall# from x8        │
        │  - Get args from x0-x2         │
        └────────────┬───────────────────┘
                     │
                     ↓ syscall_handle(sys#, arg0, arg1, arg2)
        ┌────────────────────────────────┐
        │  syscall.c:syscall_handle()    │
        │  - Dispatch to handler         │
        │  - Execute kernel operation    │
        │  - Return result               │
        └────────────┬───────────────────┘
                     │
                     ↓ Store result in regs->regs[0]
        ┌────────────────────────────────┐
        │  panic.c:exception_c_handler() │
        │  - Store return value in x0    │
        │  - Increment PC (elr += 4)     │
        └────────────┬───────────────────┘
                     │
                     ↓
        ┌────────────────────────────────┐
        │  vectors.S:kernel_exit         │
        │  - Restore regs (x0 modified)  │
        │  - eret → return to user       │
        └────────────┬───────────────────┘
                     │
                     ↓
┌────────────────────────────────────────────────────────────┐
│         User task continues after svc (PC+4)               │
│         x0 contains syscall return value                   │
└────────────────────────────────────────────────────────────┘
```

## Data Structure Cross-Reference

### `struct task_context` (sched.h)
Used by: `swtch.S:cpu_switch_to()`

Stores callee-saved registers for context switching:
- x19-x28: General purpose callee-saved
- x29: Frame pointer
- x30: Return address
- sp: Stack pointer

**Size:** 104 bytes (13 × 8 bytes)

**Layout matches assembly offsets in swtch.S**

### `struct pt_regs` (irq.h)
Used by: `panic.c:exception_c_handler()`

Captures complete CPU state at exception:
- regs[30]: x0-x29
- lr: x30 (Link Register)
- elr: Exception Link Register (PC)
- spsr: Saved Program Status Register

**Size:** 264 bytes (33 × 8 bytes)

**Created by vectors.S:kernel_entry on stack**

## Key Concepts

### Context Switching
The mechanism by which the scheduler switches between tasks:
1. Save current task's callee-saved registers
2. Switch stack pointers
3. Restore next task's registers
4. Return to next task's execution point

See: [swtch.md](swtch.md)

### Exception Handling
Processing hardware exceptions and software interrupts:
- Hardware exceptions: page faults, illegal instructions
- Software interrupts: system calls (svc instruction)
- Interrupt requests: timer, devices

See: [panic.md](panic.md)

### Task Initialization
How new tasks start executing:
1. Task struct allocated, context initialized
2. x19 = function, x20 = arg, x30 = ret_from_fork
3. First context switch loads context
4. Returns to ret_from_fork
5. ret_from_fork enables interrupts and calls function

See: [swtch.md](swtch.md) and [init.md](init.md)

### Initialization Order
Critical dependencies during boot:
1. **Memory first** - Required for everything else
2. **MMU next** - Uses memory for page tables
3. **Devices** - Need memory for buffers
4. **Scheduler** - Needs timer for preemption
5. **Syscalls** - Needed by init task
6. **Init task** - First user-level code

See: [kernel.md](kernel.md)

## Debugging Guide

### Panic Output Analysis

When a panic occurs, you'll see:
```
[PANIC] <error message>
Backtrace:
  0xAAAAAAAA
  0xBBBBBBBB
  0xCCCCCCCC
System halted.
Task: 0xTTTTTTTT
```

**To debug:**
1. Note the error message (what went wrong)
2. Use `addr2line` to resolve addresses:
   ```bash
   aarch64-none-elf-addr2line -e kernel.elf 0xAAAAAAAA
   ```
3. Check which task was running (Task ID)
4. Examine source at resolved locations

See: [panic.md](panic.md)

### Exception Type Reference

| Type | Exception | Typical Causes |
|------|-----------|----------------|
| 0    | el1_sync_invalid | Kernel mode exception |
| 1    | el1_irq | Interrupt in kernel |
| 4    | el0_sync | User mode exception/syscall |
| 5    | el0_irq | Interrupt in user mode |

See: [panic.md](panic.md)

### Context Structure Debugging

To verify context structure alignment:
```c
#include <stddef.h>
_Static_assert(offsetof(struct task_context, x19) == 0, "x19");
_Static_assert(offsetof(struct task_context, x20) == 8, "x20");
_Static_assert(offsetof(struct task_context, sp) == 96, "sp");
```

See: [swtch.md](swtch.md)

## Implementation Notes

### ARM64 Specifics
- Uses AAPCS64 calling convention
- Callee-saved: x19-x28, x29 (FP), x30 (LR), SP
- Caller-saved: x0-x18
- 16-byte stack alignment required

### Interrupt Masking
- `daif` register controls interrupt masks
- Bit 1 (I) controls IRQ
- `msr daifclr, #2` enables IRQ
- `msr daifset, #2` disables IRQ

### Exception Levels
- EL0: User mode (applications)
- EL1: Kernel mode (OS)
- EL2: Hypervisor (unused)
- EL3: Secure monitor (unused)

MYRASPOS runs in EL1, user tasks in EL0.

## Further Reading

### Related Documentation
- `docs/arch/arm64.md` - ARM64 architecture details
- `docs/arch/vectors.md` - Exception vector implementation
- `docs/sched/scheduler.md` - Task scheduler
- `docs/syscall/syscall.md` - System call interface
- `docs/memory/` - Memory management subsystems
- `docs/drivers/` - Device drivers

### Source Files
- `kernel/kernel.c` - Main kernel entry
- `kernel/init.c` - Init task
- `kernel/swtch.S` - Context switching
- `kernel/panic.c` - Exception handling
- `kernel/sched.c` - Scheduler
- `boot/start.s` - Bootloader
- `kernel/vectors.S` - Exception vectors

## Contributing

When documenting new core kernel components:
1. Follow the template structure (Overview, Boot Flow, Data Structures, etc.)
2. Include code examples and diagrams
3. Document design decisions and tradeoffs
4. Add cross-references to related components
5. Provide debugging guidance

## Revision History

- **2024-02-06**: Initial comprehensive documentation created
  - kernel.md: Main kernel initialization
  - init.md: Init task (PID 1)
  - swtch.md: Context switching assembly
  - panic.md: Panic and exception handling
  - README.md: This index document

---

**Navigation:**
- [← Back to docs/kernel](../README.md)
- [↑ Back to docs](../../README.md)
- [→ Architecture docs](../arch/README.md)
