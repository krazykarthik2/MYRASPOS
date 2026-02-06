# MYRASPOS System Architecture

## Executive Summary

MYRASPOS is a **preemptive multitasking microkernel** for AArch64 (ARM64) designed for QEMU's virt machine and Raspberry Pi 3. It features a **round-robin task scheduler**, **hierarchical memory management** (palloc + kmalloc), **MMU-based address translation**, and **interrupt-driven polled I/O**. The design prioritizes simplicity and debugging visibility over performance.

## System Overview

### Target Platform
- **Architecture**: AArch64 (ARMv8-A 64-bit)
- **Primary Target**: QEMU virt machine
- **Secondary Target**: Raspberry Pi 3 (BCM2837)
- **Exception Level**: EL1 (Kernel mode)

### Key Features
- Preemptive multitasking with round-robin scheduling
- Virtual memory management with MMU support
- Two-tier memory allocation (page + heap)
- Interrupt-driven I/O with GIC (Generic Interrupt Controller)
- RAM-based and disk-based filesystems
- Window manager with framebuffer graphics
- Shell with 24+ built-in commands
- Multiple user applications (editor, file manager, calculator, etc.)

## Core Architecture Components

### 1. Boot Sequence
```
Power On → start.s (Assembly)
    ├─ Set Exception Level (EL1)
    ├─ Configure Stack Pointer
    ├─ Clear BSS Section
    ├─ Set Exception Vectors
    └─ Jump to kernel_main()
           ↓
    kernel_main() (C code)
    ├─ Initialize Memory (palloc, kmalloc)
    ├─ Enable MMU
    ├─ Initialize Filesystems (ramfs, diskfs)
    ├─ Initialize I/O (UART, Timer, IRQ, Framebuffer)
    ├─ Initialize Scheduler
    ├─ Create Init Task
    └─ Enter Scheduling Loop
```

### 2. Memory Architecture

#### Physical Memory Layout
```
0x00000000 - 0x40000000  (1 GB)    Peripheral Space
    ├─ UART @ 0x09000000
    ├─ GIC Distributor @ 0x08000000
    ├─ GIC CPU Interface @ 0x08010000
    ├─ VirtIO Devices @ 0x0A000000+
    └─ RAMFB @ 0x42000000

0x40000000 - 0x60000000  (512 MB)  RAM
    ├─ Kernel Code @ 0x40000000
    ├─ Kernel BSS @ 0x40800000
    ├─ Heap Start @ 0x43000000
    └─ Stack Top @ 0x60000000 (grows down)
```

#### Virtual Address Space (48-bit)
```
Kernel Space (L0 Index 0):
  0x0000000000000000 - 0x000000003FFFFFFF  Peripherals (identity-mapped)
  0x0000000040000000 - 0x000000005FFFFFFF  RAM (identity-mapped)

User Space (L0 Index 1, if enabled):
  0x0000008000000000 - 0x0000FFFFFFFFFFFF  User programs
```

#### Memory Subsystems
1. **Page Allocator (palloc)**: Bitmap-based physical page allocation
2. **Kernel Heap (kmalloc)**: Free-list allocator with coalescing
3. **MMU**: 4-level page tables (L0-L3) with 4KB pages

### 3. Task Management

#### Task Structure
```c
struct task {
    int id;                    // Unique task ID
    task_fn fn;                // Entry function (NULL = blocked)
    void *arg;                 // Function argument
    char name[16];             // Task name
    void *stack;               // Kernel stack (with guard page)
    size_t stack_total_bytes;  // Stack size
    struct task_context context; // Saved CPU state
    uint64_t *pgd;             // Page table (NULL = kernel task)
    int parent_id;             // Parent task ID
    struct task *next;         // Circular linked list
    uint32_t magic;            // 0xDEADC0DE (corruption detection)
    // ... more fields for blocking, timing, etc.
};
```

#### Task States
- **Running**: Currently executing (fn != NULL, is_running = 1)
- **Runnable**: Ready to execute (fn != NULL, is_running = 0)
- **Blocked**: Waiting on event (fn == NULL)
- **Zombie**: Exited, pending cleanup (zombie = 1)

#### Scheduling Algorithm
**Round-Robin Cooperative Preemption**:
1. Poll timers and I/O devices
2. Advance system tick count
3. Wake sleeping tasks if time expired
4. Select next runnable task from circular list
5. Context switch via `cpu_switch_to()`
6. Return to user code

**Preemption**: Timer interrupts set a flag; actual context switch occurs in next scheduling cycle.

### 4. Interrupt Handling

#### Exception Vectors (AArch64)
```
VBAR_EL1 → vectors (11-bit aligned)
    ├─ [0x000] Current EL with SP0: Sync, IRQ, FIQ, SError
    ├─ [0x200] Current EL with SPx: Sync, IRQ, FIQ, SError
    ├─ [0x400] Lower EL (AArch64): Sync, IRQ, FIQ, SError
    └─ [0x600] Lower EL (AArch32): Sync, IRQ, FIQ, SError
```

#### Interrupt Flow
1. Hardware interrupt triggers exception
2. CPU saves state to ELR_EL1, SPSR_EL1
3. Vector entry saves all registers to stack (pt_regs)
4. Call `exception_c_handler()`
5. Dispatch to registered IRQ handler
6. Send EOI (End of Interrupt) to GIC
7. Request preemption
8. Restore registers and return via `eret`

#### GIC (Generic Interrupt Controller)
- **Distributor (GICD)**: Routes interrupts to CPU interfaces
- **CPU Interface (GICC)**: Presents interrupts to CPU
- **IRQ Numbers**: 0-31 (Private), 32-1019 (Shared)
- **Configuration**: All SPIs masked by default; poll-based model

### 5. I/O Subsystems

#### UART (Serial Console)
- **Base Address**: 0x09000000 (QEMU PL011)
- **Mode**: Polled I/O with interrupt capability
- **Functions**: Character I/O, string output, debugging

#### Timer
- **Implementation**: CNTPCT_EL0 (ARM generic timer)
- **Resolution**: Milliseconds
- **Functions**: Sleep, timeouts, system uptime tracking

#### Framebuffer
- **Backends**: VirtIO-GPU (preferred), RAMFB (fallback)
- **Format**: 32-bit RGBA
- **Functions**: Pixel manipulation, rectangle drawing, text rendering

#### Input
- **Keyboard**: PS/2 or VirtIO keyboard
- **Mouse**: PS/2 or VirtIO mouse  
- **Event Queue**: Circular buffer with blocking read support

### 6. Filesystem Architecture

#### VFS (Virtual File System)
```c
struct file {
    int fd;           // File descriptor
    int flags;        // O_RDONLY, O_WRONLY, O_RDWR
    int pos;          // Current position
    void *impl;       // Backend-specific data
    struct file_ops *ops;  // Function pointers
};
```

#### Supported Filesystems
1. **RamFS**: In-memory filesystem with directories
   - Uses linked lists for directory entries
   - No persistence across reboots
   - Fast access

2. **DiskFS**: Simple disk-based filesystem
   - VirtIO block device backend
   - Fixed-size file entries
   - Persists across reboots

#### File Operations
- `open()`, `close()`, `read()`, `write()`
- `mkdir()`, `rmdir()`, `readdir()`
- `stat()`, `unlink()`

### 7. Window Manager

#### Architecture
```
Window Manager (wm.c)
    ├─ Window List (linked list)
    ├─ Focus Management
    ├─ Event Dispatch
    └─ Rendering Pipeline
           ├─ Clear Background
           ├─ Render Windows (back-to-front)
           └─ Render Taskbar
```

#### Window Structure
```c
struct window {
    int id;
    char title[64];
    int x, y, width, height;
    uint32_t border_color, bg_color;
    void (*render_cb)(struct window *);
    void (*keyboard_cb)(struct window *, char);
    void (*mouse_cb)(struct window *, int, int, int);
    void *userdata;
    struct window *next;
};
```

### 8. System Calls

#### Syscall Interface
- **Entry**: `svc #0` instruction
- **Handler**: `handle_sync_el64()` in vectors.S
- **Dispatcher**: `syscall_c()` in syscall.c
- **Arguments**: x0-x6 (up to 7 arguments)
- **Return**: x0

#### Available Syscalls
```c
#define SYS_EXIT        0
#define SYS_FORK        1
#define SYS_EXEC        2
#define SYS_WAIT        3
#define SYS_OPEN        4
#define SYS_CLOSE       5
#define SYS_READ        6
#define SYS_WRITE       7
#define SYS_GETCHAR     8
// ... more syscalls
```

## Design Philosophy

### Simplicity Over Performance
- **Clear code structure**: Easy to understand and modify
- **Simple algorithms**: Round-robin, first-fit, linear search
- **Minimal optimization**: No premature optimization
- **Extensive debugging**: UART logging, stack guards, magic numbers

### Educational Focus
- **Commented code**: Extensive inline documentation
- **Standard patterns**: Familiar OS design patterns
- **No magic**: Everything explicit and traceable
- **Learning-friendly**: Good starting point for OS development

### Reliability Through Defense
- **Stack guards**: 4KB guard page + canary value
- **Magic numbers**: Struct corruption detection
- **Pointer validation**: NULL checks, alignment checks
- **Safe defaults**: Conservative memory limits

## Performance Characteristics

### Time Complexity
- **Scheduling**: O(N) where N = number of tasks
- **Memory Allocation**:
  - Page alloc: O(N) where N = number of pages
  - kmalloc: O(M) where M = number of free blocks
- **Context Switch**: O(1)
- **IRQ Dispatch**: O(H) where H = number of registered handlers

### Memory Overhead
- **Per-Task**: ~272 bytes (struct) + stack (16-64KB)
- **Page Tables**: ~4KB per task (L0 table)
- **Page Bitmap**: 32KB (for 256K pages = 1GB)
- **Free List**: Variable (depends on fragmentation)

## Constraints and Limitations

### Hard Constraints
1. **Single-core only**: No SMP support
2. **EL1 only**: No support for running at EL2/EL0
3. **AArch64 only**: No 32-bit AArch32 support
4. **Fixed memory**: 512MB RAM expected
5. **Polled I/O**: No true interrupt-driven model

### Soft Constraints
1. **Maximum tasks**: Limited by memory (~1000s)
2. **Maximum pages**: 256K pages = 1GB
3. **Stack size**: 16KB default, 64KB for init
4. **File path length**: 256 characters
5. **Window title**: 64 characters

### Known Limitations
1. **No ASLR**: Tasks load at predictable addresses
2. **No DEP**: Execute from any memory region
3. **No SMP**: Single CPU only
4. **Fragmentation**: Page allocator doesn't coalesce
5. **Latency**: Polled I/O has higher latency
6. **No signals**: No POSIX signal support
7. **Limited syscalls**: Minimal system call interface

## Security Considerations

### Memory Protection
- **MMU enabled**: Virtual memory separation
- **User/Kernel split**: AP bits control access
- **Stack guards**: Detect stack overflow/underflow
- **No execute**: NX bit on data pages (configurable)

### Limitations
- **Shared kernel mapping**: Kernel accessible from user space
- **No ASLR**: Predictable memory layout
- **Limited isolation**: Simple user/kernel split
- **No capabilities**: Root-only security model

## Debugging Features

### Built-in Debugging
1. **UART logging**: Extensive debug output via serial
2. **Task tracing**: Log all task creation, blocking, exit
3. **Context dumps**: Print CPU registers on panic
4. **Stack validation**: Check guards on every context switch
5. **Magic numbers**: Detect struct corruption

### Debug Commands
- `ps`: List all tasks with status
- `free`: Show memory allocation statistics
- `systemctl status`: Service status
- Stack traces on panic

## Future Directions

### Planned Improvements
1. **Networking stack**: TCP/IP, WiFi support
2. **Performance**: Object pools, dirty rectangles
3. **Editor**: Vim-style text editor
4. **Better scheduling**: Priority-based scheduling
5. **True preemption**: Interrupt-driven scheduling

### Not Planned (Out of Scope)
1. Multi-core/SMP support
2. HTTPS/TLS encryption
3. Complex graphics (GPU acceleration)
4. POSIX compliance
5. Production deployment

## References

- ARM Architecture Reference Manual ARMv8
- Raspberry Pi 3 BCM2837 Technical Reference  
- QEMU virt machine specification
- VirtIO specification v1.1
- GIC Architecture Specification v2
- LodePNG documentation

---

**See Also**:
- [Boot Sequence](01-BOOT-SEQUENCE.md)
- [Memory Management](02-MEMORY-MANAGEMENT.md)
- [Task Scheduling](03-TASK-SCHEDULING.md)
- [Design Decisions](05-DESIGN-DECISIONS.md)
