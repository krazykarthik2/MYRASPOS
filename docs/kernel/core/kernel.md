# kernel.c/h - Main Kernel Entry Point and Initialization

## Overview

`kernel.c` and `kernel.h` contain the main kernel entry point (`kernel_main()`) that orchestrates the initialization of all kernel subsystems and starts the scheduler loop. This is the central coordination point called by the bootloader after basic CPU/stack setup.

**Purpose:**
- Initialize all kernel subsystems in correct dependency order
- Set up memory management (physical and virtual)
- Initialize device drivers (UART, framebuffer, virtio)
- Configure interrupt handling and scheduling
- Launch the init task (PID 1)
- Enter the main scheduler loop

**Initialization Sequence:**
The kernel follows a strict initialization order to respect dependencies between subsystems:
1. Memory subsystems (palloc, kmalloc) - Required first for page table allocation
2. MMU (Memory Management Unit) - Enables consistent caching
3. Filesystems (ramfs, diskfs)
4. Services framework
5. Graphics (virtio-gpu or RAMFB fallback)
6. Timer and IRQ subsystems
7. Scheduler initialization
8. Syscall registration
9. Init task creation
10. Enable interrupts and enter scheduler loop

## Boot Flow

### 1. Bootloader to Kernel Transition
```
boot/start.s (_start) → kernel_main()
```

The bootloader (`start.s`) performs:
- CPU exception level check (ensure EL1)
- Set exception vector table (`vbar_el1`)
- Initialize stack pointer
- Clear BSS section
- Jump to `kernel_main()`

### 2. Memory Initialization
```c
uintptr_t ram_end   = 0x60000000;  // 1.5 GB RAM end
uintptr_t heap_base = 0x43000000;  // ~1 GB heap start
size_t heap_pages = (ram_end - heap_base) / PAGE_SIZE;

palloc_init((void *)heap_base, heap_pages);  // Physical page allocator
kmalloc_init();                               // Kernel heap allocator
```

**Memory Layout:**
- `0x40000000` - Kernel code/data
- `0x42000000` - Framebuffer (RAMFB or virtio-mapped)
- `0x43000000` - Heap start (managed by palloc/kmalloc)
- `0x60000000` - RAM end

### 3. MMU Enablement
```c
mmu_init();
```
Enables virtual memory with identity mapping and consistent cache policy across all memory regions.

### 4. Filesystem Initialization
```c
ramfs_init();   // In-memory filesystem
diskfs_init();  // Persistent disk-backed filesystem
```

### 5. Service Manager Initialization
```c
services_init();
```
Sets up the systemd-like service management framework.

### 6. Graphics Initialization
The kernel attempts to initialize graphics in order of preference:

**Primary: virtio-gpu**
```c
if (virtio_gpu_init() == 0) {
    int w = virtio_gpu_get_width();
    int h = virtio_gpu_get_height();
    fb_init((void *)0x42000000, w, h, w*4);
}
```

**Fallback: RAMFB**
```c
else {
    // Probe RAMFB by writing test pattern
    volatile uint32_t *probe = (volatile uint32_t *)0x42000000;
    for (int i = 0; i < 1024; ++i) {
        probe[i] = (uint32_t)(0xA5A50000 | i);
    }
    // Verify checksum
    fb_init((void *)0x42000000, 800, 600, 800*4);
}
```

Display startup message:
```c
if (fb_is_init()) {
    fb_fill(0x000000);
    fb_put_text_centered("HELLO FROM MYRAS", 0xFFFFFFFF);
}
```

### 7. Timer and Interrupt Setup
```c
timer_init();  // ARM Generic Timer
irq_init();    // GIC (Generic Interrupt Controller)
scheduler_init();
```

### 8. Syscall Registration
```c
syscall_init();
syscall_register_defaults();
```

### 9. Init Task Creation
```c
task_create_with_stack(init_main, NULL, "init", 64);
```
Creates the init task (PID 1) with a large 64 KB stack for bootup operations.

### 10. Enable Interrupts and Enter Scheduler
```c
__asm__ volatile("msr daifclr, #2");  // Enable IRQ interrupts

while (1) {
    schedule();
}
```

## Data Structures

### Linker Symbols
```c
extern char stack_top[];  // Top of boot stack (defined in linker script)
```

### External References
```c
extern void init_main(void *arg);  // Init task entry point (init.c)
```

## Key Functions

### `void kernel_main(void)`

**Description:** Main kernel entry point called by bootloader after basic CPU setup.

**Prototype:**
```c
void kernel_main(void);
```

**Parameters:** None

**Returns:** Never returns (infinite scheduler loop)

**Call Graph:**
```
_start (start.s)
  └─> kernel_main()
       ├─> palloc_init()
       ├─> kmalloc_init()
       ├─> mmu_init()
       ├─> ramfs_init()
       ├─> diskfs_init()
       ├─> services_init()
       ├─> virtio_gpu_init() / fb_init()
       ├─> timer_init()
       ├─> irq_init()
       ├─> scheduler_init()
       ├─> syscall_init()
       ├─> task_create_with_stack(init_main, ...)
       └─> while(1) { schedule(); }
```

**Critical Sections:**
- Memory initialization must happen before MMU (page tables need memory)
- Timer/IRQ must be initialized before scheduler
- Interrupts must be enabled after scheduler is ready

### `void user_test_entry(void)` [DISABLED]

**Description:** Test user-space task implementation (currently disabled to prevent log spam).

**Prototype:**
```c
void __attribute__((naked)) user_test_entry(void);
```

**Implementation:**
- Constructs string "H User!\n" on stack using MOVZ/MOVK instructions
- Repeatedly calls syscall #1 (SYS_PUTS) to print the message
- Calls syscall #2 (SYS_YIELD) to yield CPU
- Includes delay loop between iterations
- Demonstrates user-space execution and syscall mechanism

**Usage:**
```c
// Disabled in kernel_main():
// task_create_user("user_test", user_test_entry);
```

## Implementation Details

### Memory Region Assignment
The kernel uses a fixed memory layout for simplicity:

| Address Range | Purpose | Size |
|---------------|---------|------|
| 0x40000000 - 0x41FFFFFF | Kernel code/data/stack | ~32 MB |
| 0x42000000 - 0x42FFFFFF | Framebuffer | ~16 MB |
| 0x43000000 - 0x5FFFFFFF | Heap (palloc/kmalloc) | ~448 MB |

### Graphics Initialization Strategy
1. **Try virtio-gpu first** - Modern, flexible, provides resolution info
2. **Fall back to RAMFB** - Fixed address, requires probing
3. **Probe with test pattern** - Ensures memory is actually framebuffer
4. **Calculate checksum** - Verifies write/read consistency

### Interrupt Enable Timing
Interrupts are disabled during boot (by `start.s`) and remain disabled through all initialization. They are only enabled after:
- All subsystems are initialized
- Init task is created
- Scheduler is ready to handle preemption

This prevents spurious interrupts from disrupting initialization.

### Stack Size Considerations
- Boot stack: Defined by linker script (~64 KB typically)
- Init task stack: 64 KB (large due to complex bootup operations)
- Regular task stacks: 16 KB default (configurable)

## Design Decisions

### Why Initialize Memory First?
The MMU needs page tables, which require dynamic memory allocation. Therefore, palloc and kmalloc must be initialized before `mmu_init()`.

### Why Enable MMU Early?
The MMU provides:
- **Consistent caching policy** - Critical for DMA and device memory
- **Memory protection** (future) - Foundation for user/kernel separation
- **Performance** - Enables instruction/data caches uniformly

### Why Graphics Before Scheduler?
- Provides early visual feedback to user
- Simplifies initialization (no concurrency concerns)
- Graphics device discovery can take time

### Why Large Init Stack?
The init task performs complex operations:
- VirtIO initialization (large state machines)
- Service manager setup (parsing, allocation)
- Filesystem operations (directories, files)
- GUI initialization (window manager, input)

A large stack prevents overflow during these operations.

### Why Infinite Scheduler Loop in Kernel?
The kernel never exits. The scheduler loop is the "main loop" of the OS:
```c
while (1) {
    schedule();  // Pick next task and switch to it
}
```

When tasks yield or are preempted, control returns here.

## Usage Examples

### Boot Sequence Trace
```
[bootloader] _start: EL1 confirmed
[bootloader] Stack at 0x40100000
[bootloader] BSS cleared
[bootloader] Jumping to kernel_main...

[kernel] initializing memory subsystems...
[kernel] palloc: 117440 pages available
[kernel] kmalloc: heap ready
[kernel] enabling MMU...
[kernel] MMU: identity map 0x00000000-0x60000000
[kernel] initializing ramfs...
[kernel] initializing diskfs...
[kernel] initializing services...
[kernel] probing graphics...
[kernel] virtio-gpu initialized (800x600)
[kernel] enabling interrupts...
[kernel] scheduler: init task created (PID 1)
[kernel] entering scheduler loop...

[init] starting services...
[init] GUI subsystem starting...
[init] starting shell...
```

### Adding a New Subsystem
To add a new subsystem initialization:

```c
void kernel_main(void) {
    /* ... existing initialization ... */
    
    uart_puts("[kernel] initializing my_subsystem...\n");
    my_subsystem_init();  // Your initialization function
    
    /* Continue with scheduler init ... */
    scheduler_init();
    /* ... */
}
```

**Important:** Add it in the correct dependency order:
- After memory if it needs dynamic allocation
- After MMU if it accesses device memory
- Before scheduler if tasks depend on it

### Accessing Kernel Entry from Other Files
```c
// kernel.h
#ifndef KERNEL_H
#define KERNEL_H

void kernel_main(void);

#endif
```

## Cross-References

### Related Documentation
- [init.md](init.md) - Init task (PID 1) implementation
- [swtch.md](swtch.md) - Context switching assembly
- [panic.md](panic.md) - Panic handler and exception processing
- [../memory/mmu.md](../memory/mmu.md) - MMU initialization
- [../memory/palloc.md](../memory/palloc.md) - Physical page allocator
- [../memory/kmalloc.md](../memory/kmalloc.md) - Kernel heap allocator
- [../sched/scheduler.md](../sched/scheduler.md) - Task scheduler
- [../syscall/syscall.md](../syscall/syscall.md) - System call interface
- [../drivers/framebuffer.md](../drivers/framebuffer.md) - Graphics initialization
- [../drivers/virtio.md](../drivers/virtio.md) - VirtIO GPU driver
- [../drivers/timer.md](../drivers/timer.md) - ARM Generic Timer
- [../drivers/irq.md](../drivers/irq.md) - Interrupt controller

### Source Code Dependencies

**Includes:**
```c
#include "uart.h"         // Early console output
#include "kmalloc.h"      // Kernel heap allocator
#include "palloc.h"       // Physical page allocator
#include "sched.h"        // Scheduler and task creation
#include "syscall.h"      // System call registration
#include "ramfs.h"        // In-memory filesystem
#include "timer.h"        // System timer
#include "irq.h"          // Interrupt controller
#include "framebuffer.h"  // Graphics output
#include "virtio.h"       // VirtIO GPU driver
#include "service.h"      // Service manager
#include "mmu.h"          // MMU and page tables
#include "diskfs.h"       // Disk filesystem
```

### Boot Flow Sequence
```
boot/start.s:_start
  │
  ├─ Set EL1
  ├─ Setup exception vectors (vbar_el1)
  ├─ Setup stack (stack_top)
  ├─ Clear BSS
  │
  └─> kernel.c:kernel_main()
       │
       ├─> palloc_init()
       ├─> kmalloc_init()
       ├─> mmu_init()
       ├─> ramfs_init()
       ├─> diskfs_init()
       ├─> services_init()
       ├─> virtio_gpu_init() / fb_init()
       ├─> timer_init()
       ├─> irq_init()
       ├─> scheduler_init()
       ├─> syscall_init()
       ├─> task_create_with_stack(init_main, ...)
       │
       └─> while(1) { schedule(); }
            │
            └─> swtch.S:cpu_switch_to() → Tasks execute
```

### Task Creation Flow
```
kernel_main()
  │
  └─> task_create_with_stack(init_main, NULL, "init", 64)
       │
       └─> sched.c:task_create_with_stack()
            │
            ├─ Allocate task struct
            ├─ Allocate stack (64 KB)
            ├─ Initialize task_context
            │   ├─ x19 = init_main (function pointer)
            │   ├─ x20 = NULL (argument)
            │   ├─ x30 = ret_from_fork (return address)
            │   └─ sp = stack_top
            │
            └─> Add to scheduler ready queue
```

### Memory Initialization Dependencies
```
kernel_main()
  │
  ├─> palloc_init()          [1. Physical page allocator]
  │    └─ Manages free pages in bitmap
  │
  ├─> kmalloc_init()         [2. Kernel heap allocator]
  │    └─ Uses palloc for backing pages
  │
  └─> mmu_init()             [3. MMU setup]
       └─ Uses kmalloc for page tables
```

This dependency order is critical and must not be changed.
