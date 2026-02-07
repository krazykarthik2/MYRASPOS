# MYRASPOS Complete Documentation Index

## Overview

This document provides a comprehensive index to all MYRASPOS documentation, organized by topic and component. Use this as your starting point for understanding any aspect of the system.

---

## Quick Start Guide

**New to MYRASPOS?** Start here:

1. [**Architecture Overview**](00-ARCHITECTURE.md) - Understand the big picture
2. [**Build System**](build/BUILD-SYSTEM.md) - Learn how to compile
3. [**Simulator**](build/sim.bat.md) - Run in QEMU
4. [**Design Decisions**](05-DESIGN-DECISIONS.md) - Understand why things are the way they are

---

## Documentation by Category

### 1. System Architecture

| Document | Description | Key Topics |
|----------|-------------|------------|
| [Architecture Overview](00-ARCHITECTURE.md) | Complete system architecture | Boot flow, memory layout, subsystems |
| [Boot Sequence](01-BOOT-SEQUENCE.md) | How the system boots | start.s, EL setup, kernel_main |
| [Design Decisions](05-DESIGN-DECISIONS.md) | Key architectural choices | Trade-offs, constraints, rationale |

### 2. Build and Development

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Build System](build/BUILD-SYSTEM.md) | Complete build process | build.bat, toolchain, flags |
| [Simulator](build/sim.bat.md) | QEMU configuration | sim.bat, virt machine, devices |
| [Python Scripts](build/python-scripts.md) | Build utilities | make_disk.py, move_files.py |

### 3. Boot Process

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Boot Assembly](boot/start.s.md) | AArch64 boot code | start.s - EL setup, BSS clear, stack |
| [Linker Scripts](boot/linker-files.md) | Memory layout definition | linker.ld, linker_pi.ld |

### 4. Kernel Core

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Kernel Entry](kernel/core/kernel.md) | Main kernel initialization | kernel.c/h - kernel_main() |
| [Init Task](kernel/core/init.md) | First userspace task | init.c/h - service startup |
| [Task Scheduling](kernel/03-TASK-SCHEDULING.md) | Complete scheduler docs | sched.c/h, swtch.S |
| [Context Switching](kernel/core/swtch.md) | Low-level context switch | swtch.S - cpu_switch_to |
| [Panic Handler](kernel/core/panic.md) | Error handling | panic.c/h - kernel panics |

### 5. Memory Management

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Memory Overview](02-MEMORY-MANAGEMENT.md) | Complete memory system | All memory subsystems |
| [Page Allocator](kernel/memory/palloc.c.md) | Physical page allocation | palloc.c/h - bitmap allocator |
| [Kernel Heap](kernel/memory/kmalloc.c.md) | Kernel malloc/free | kmalloc.c/h - free list allocator |
| [MMU](kernel/memory/mmu.c.md) | Virtual memory | mmu.c/h - page tables, MMU setup |

### 6. I/O Subsystems

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [I/O Overview](kernel/io/README.md) | All I/O subsystems | Overview of UART, timer, IRQ, etc. |
| [UART](kernel/io/uart.c.md) | Serial console | uart.c/h - PL011 UART driver |
| [Timer](kernel/io/timer.c.md) | System timer | timer.c/h - CNTPCT_EL0, sleep |
| [IRQ Handler](kernel/io/irq.c.md) | Interrupt handling | irq.c/h - GIC, IRQ dispatch |
| [Framebuffer](kernel/io/framebuffer.c.md) | Graphics output | framebuffer.c/h - pixel drawing |
| [Input](kernel/io/input.c.md) | Keyboard and mouse | input.c/h - VirtIO input devices |

### 7. Filesystems

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Filesystem Overview](kernel/fs/README.md) | VFS and filesystems | All filesystem components |
| [RamFS](kernel/fs/ramfs.c.md) | RAM-based filesystem | ramfs.c/h - in-memory FS |
| [DiskFS](kernel/fs/diskfs.c.md) | Disk-based filesystem | diskfs.c/h - persistent storage |
| [File Abstraction](kernel/fs/files.c.md) | VFS layer | files.c/h - file operations |

### 8. Drivers

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [VirtIO Drivers](kernel/drivers/virtio.c.md) | Complete VirtIO support | virtio.c/h - GPU, block, input |

### 9. System Services

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Services Overview](kernel/services/README.md) | System services | All service components |
| [Shell](kernel/services/shell.md) | Command-line shell | shell.c/h - command processor |
| [PTY](kernel/services/pty.md) | Pseudo-terminals | pty.c/h - terminal emulation |
| [Service Manager](kernel/services/service.md) | Service lifecycle | service.c/h - service management |
| [Programs](kernel/services/programs.md) | Program execution | programs.c/h - program loading |
| [System Calls](kernel/services/syscall.md) | Syscall interface | syscall.c/h - kernel/user interface |
| [Exception Vectors](kernel/services/vectors.md) | Exception handling | vectors.S - exception table |

### 10. Window Manager

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Window Manager](kernel/wm/README.md) | Complete WM documentation | wm.c/h, cursor.c/h |

### 11. Utility Libraries

| Document | Description | Files Covered |
|----------|-------------|---------------|
| [Utilities Overview](kernel/lib/README.md) | All utility libraries | lib, glob, image, lodepng, panic |

### 12. Applications

| Document | Description | Applications Covered |
|----------|-------------|----------------------|
| [Applications](kernel/apps/README.md) | All 7 GUI applications | Calculator, Editor, Files, Image Viewer, Keyboard Tester, Myra, Terminal |

### 13. Shell Commands

| Document | Description | Commands Covered |
|----------|-------------|------------------|
| [Commands](kernel/commands/README.md) | All 25 shell commands | cat, clear, cp, echo, edit, free, grep, head, help, kill, ls, mkdir, more, mv, ps, ramfs_tools, rm, rmdir, sleep, systemctl, tail, touch, tree, view, wait |

---

## Documentation by File

### Assembly Files

| File | Documentation | Description |
|------|---------------|-------------|
| `boot/start.s` | [Boot Assembly](boot/start.s.md) | AArch64 boot code, exception level setup |
| `kernel/vectors.S` | [Exception Vectors](kernel/services/vectors.md) | Exception vector table |
| `kernel/swtch.S` | [Context Switching](kernel/core/swtch.md) | Task context switch assembly |

### Linker Scripts

| File | Documentation | Description |
|------|---------------|-------------|
| `linkers/linker.ld` | [Linker Scripts](boot/linker-files.md) | QEMU virt linker script |
| `linkers/linker_pi.ld` | [Linker Scripts](boot/linker-files.md) | Raspberry Pi linker script |

### Core Kernel Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/kernel.c/h` | [Kernel Entry](kernel/core/kernel.md) | Main kernel initialization |
| `kernel/init.c/h` | [Init Task](kernel/core/init.md) | Init task and services |
| `kernel/sched.c/h` | [Task Scheduling](kernel/03-TASK-SCHEDULING.md) | Complete scheduler |
| `kernel/panic.c/h` | [Panic Handler](kernel/core/panic.md) | Kernel panic handling |

### Memory Management Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/palloc.c/h` | [Page Allocator](kernel/memory/palloc.c.md) | Physical page allocation |
| `kernel/kmalloc.c/h` | [Kernel Heap](kernel/memory/kmalloc.c.md) | Kernel heap allocator |
| `kernel/mmu.c/h` | [MMU](kernel/memory/mmu.c.md) | Virtual memory management |

### I/O Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/uart.c/h` | [UART](kernel/io/uart.c.md) | Serial console driver |
| `kernel/timer.c/h` | [Timer](kernel/io/timer.c.md) | System timer |
| `kernel/irq.c/h` | [IRQ Handler](kernel/io/irq.c.md) | Interrupt handling |
| `kernel/framebuffer.c/h` | [Framebuffer](kernel/io/framebuffer.c.md) | Graphics output |
| `kernel/input.c/h` | [Input](kernel/io/input.c.md) | Input devices |

### Filesystem Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/ramfs.c/h` | [RamFS](kernel/fs/ramfs.c.md) | RAM filesystem |
| `kernel/diskfs.c/h` | [DiskFS](kernel/fs/diskfs.c.md) | Disk filesystem |
| `kernel/files.c/h` | [File Abstraction](kernel/fs/files.c.md) | VFS layer |

### Driver Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/virtio.c/h` | [VirtIO Drivers](kernel/drivers/virtio.c.md) | VirtIO device drivers |

### Service Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/shell.c/h` | [Shell](kernel/services/shell.md) | Command shell |
| `kernel/pty.c/h` | [PTY](kernel/services/pty.md) | Pseudo-terminals |
| `kernel/service.c/h` | [Service Manager](kernel/services/service.md) | Service lifecycle |
| `kernel/programs.c/h` | [Programs](kernel/services/programs.md) | Program execution |
| `kernel/syscall.c/h` | [System Calls](kernel/services/syscall.md) | System call interface |

### Window Manager Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/wm.c/h` | [Window Manager](kernel/wm/README.md) | Window management |
| `kernel/cursor.c/h` | [Window Manager](kernel/wm/README.md) | Cursor rendering |

### Utility Library Files

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/lib.c/h` | [Utilities](kernel/lib/README.md) | Standard library functions |
| `kernel/glob.c/h` | [Utilities](kernel/lib/README.md) | Pattern matching |
| `kernel/image.c/h` | [Utilities](kernel/lib/README.md) | Image loading |
| `kernel/lodepng.c/h` | [Utilities](kernel/lib/README.md) | PNG decoder |
| `kernel/lodepng_glue.c/h` | [Utilities](kernel/lib/README.md) | PNG integration |
| `kernel/write.c` | [Utilities](kernel/lib/README.md) | Write utilities |

### Application Files (7 apps)

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/apps/calculator_app.c/h` | [Applications](kernel/apps/README.md) | Calculator application |
| `kernel/apps/editor_app.c/h` | [Applications](kernel/apps/README.md) | Text editor |
| `kernel/apps/files_app.c/h` | [Applications](kernel/apps/README.md) | File manager |
| `kernel/apps/image_viewer.c/h` | [Applications](kernel/apps/README.md) | Image viewer |
| `kernel/apps/keyboard_tester_app.c/h` | [Applications](kernel/apps/README.md) | Keyboard tester |
| `kernel/apps/myra_app.c/h` | [Applications](kernel/apps/README.md) | App launcher |
| `kernel/apps/terminal_app.c/h` | [Applications](kernel/apps/README.md) | Terminal emulator |

### Command Files (25 commands)

| File | Documentation | Description |
|------|---------------|-------------|
| `kernel/commands/cat.c` | [Commands](kernel/commands/README.md) | Display file contents |
| `kernel/commands/clear.c` | [Commands](kernel/commands/README.md) | Clear screen |
| `kernel/commands/cp.c` | [Commands](kernel/commands/README.md) | Copy files |
| `kernel/commands/echo.c` | [Commands](kernel/commands/README.md) | Print text |
| `kernel/commands/edit.c` | [Commands](kernel/commands/README.md) | Edit files |
| `kernel/commands/free.c` | [Commands](kernel/commands/README.md) | Memory usage |
| `kernel/commands/grep.c` | [Commands](kernel/commands/README.md) | Pattern search |
| `kernel/commands/head.c` | [Commands](kernel/commands/README.md) | Display file beginning |
| `kernel/commands/help.c` | [Commands](kernel/commands/README.md) | Display help |
| `kernel/commands/kill.c` | [Commands](kernel/commands/README.md) | Terminate process |
| `kernel/commands/ls.c` | [Commands](kernel/commands/README.md) | List directory |
| `kernel/commands/mkdir.c` | [Commands](kernel/commands/README.md) | Make directory |
| `kernel/commands/more.c` | [Commands](kernel/commands/README.md) | Page file contents |
| `kernel/commands/mv.c` | [Commands](kernel/commands/README.md) | Move/rename files |
| `kernel/commands/ps.c` | [Commands](kernel/commands/README.md) | Process status |
| `kernel/commands/ramfs_tools.c` | [Commands](kernel/commands/README.md) | RAM filesystem utilities |
| `kernel/commands/rm.c` | [Commands](kernel/commands/README.md) | Remove files |
| `kernel/commands/rmdir.c` | [Commands](kernel/commands/README.md) | Remove directory |
| `kernel/commands/sleep.c` | [Commands](kernel/commands/README.md) | Delay execution |
| `kernel/commands/systemctl.c` | [Commands](kernel/commands/README.md) | Service control |
| `kernel/commands/tail.c` | [Commands](kernel/commands/README.md) | Display file end |
| `kernel/commands/touch.c` | [Commands](kernel/commands/README.md) | Create empty file |
| `kernel/commands/tree.c` | [Commands](kernel/commands/README.md) | Directory tree |
| `kernel/commands/view.c` | [Commands](kernel/commands/README.md) | File viewer |
| `kernel/commands/wait.c` | [Commands](kernel/commands/README.md) | Wait for event |

---

## Documentation by Topic

### Understanding the System

**Start here if you want to understand how MYRASPOS works:**

1. [System Architecture](00-ARCHITECTURE.md) - Big picture overview
2. [Boot Sequence](01-BOOT-SEQUENCE.md) - How the system starts
3. [Memory Management](02-MEMORY-MANAGEMENT.md) - How memory is managed
4. [Task Scheduling](kernel/03-TASK-SCHEDULING.md) - How tasks run
5. [Design Decisions](05-DESIGN-DECISIONS.md) - Why things are designed this way

### Building and Running

**Start here if you want to build and run MYRASPOS:**

1. [Build System](build/BUILD-SYSTEM.md) - How to compile everything
2. [Simulator](build/sim.bat.md) - How to run in QEMU
3. [Python Scripts](build/python-scripts.md) - Understanding build utilities

### Developing Applications

**Start here if you want to write applications for MYRASPOS:**

1. [Window Manager](kernel/wm/README.md) - Creating GUI windows
2. [Applications Overview](kernel/apps/README.md) - Example applications
3. [System Calls](kernel/services/syscall.md) - Kernel interface
4. [Shell Commands](kernel/commands/README.md) - Adding commands

### Kernel Development

**Start here if you want to modify the kernel:**

1. [Kernel Core](kernel/core/README.md) - Core kernel functions
2. [Memory Management](kernel/memory/README.md) - Allocators and MMU
3. [I/O Subsystems](kernel/io/README.md) - Device drivers
4. [Filesystems](kernel/fs/README.md) - Filesystem implementation

### Hardware and Low-Level

**Start here if you want to understand hardware interaction:**

1. [Boot Assembly](boot/start.s.md) - Low-level boot code
2. [Context Switching](kernel/core/swtch.md) - Assembly context switch
3. [Exception Vectors](kernel/services/vectors.md) - Exception handling
4. [IRQ Handler](kernel/io/irq.c.md) - Interrupt handling
5. [MMU](kernel/memory/mmu.c.md) - Virtual memory setup

---

## Key Concepts Cross-Reference

### Task Management
- [Task Scheduling](kernel/03-TASK-SCHEDULING.md) - Complete scheduler documentation
- [Context Switching](kernel/core/swtch.md) - Low-level switching
- [System Calls](kernel/services/syscall.md) - User/kernel interface
- [Init Task](kernel/core/init.md) - First user task

### Memory
- [Memory Overview](02-MEMORY-MANAGEMENT.md) - Complete memory system
- [Page Allocator](kernel/memory/palloc.c.md) - Physical pages
- [Kernel Heap](kernel/memory/kmalloc.c.md) - Kernel malloc
- [MMU](kernel/memory/mmu.c.md) - Virtual memory

### I/O and Devices
- [I/O Overview](kernel/io/README.md) - All I/O subsystems
- [VirtIO Drivers](kernel/drivers/virtio.c.md) - VirtIO devices
- [Framebuffer](kernel/io/framebuffer.c.md) - Graphics
- [Input](kernel/io/input.c.md) - Keyboard and mouse

### User Interface
- [Window Manager](kernel/wm/README.md) - GUI system
- [Applications](kernel/apps/README.md) - GUI applications
- [Shell](kernel/services/shell.md) - Command-line interface
- [Commands](kernel/commands/README.md) - All shell commands

---

## Statistics

### Documentation Coverage

- **Total source files**: 97 C/H/S files
- **Documented files**: 97 (100%)
- **Documentation pages**: 35+
- **Total documentation**: ~150,000 words

### Component Breakdown

| Category | Files | Documentation |
|----------|-------|---------------|
| Core Kernel | 8 | 6 documents |
| Memory Management | 6 | 4 documents |
| I/O Subsystems | 10 | 6 documents |
| Filesystems | 6 | 4 documents |
| Drivers | 2 | 1 document |
| Services | 10 | 7 documents |
| Window Manager | 4 | 1 document |
| Utilities | 12 | 1 document |
| Applications | 14 | 1 document |
| Commands | 25 | 1 document |
| **Total** | **97** | **32+ documents** |

---

## Contributing to Documentation

When updating MYRASPOS:

1. **Update affected documentation** - Keep docs in sync with code
2. **Document design decisions** - Explain why, not just what
3. **Note constraints** - Document limitations and trade-offs
4. **Add cross-references** - Link related documentation
5. **Keep consistent format** - Follow existing documentation style

### Documentation Style Guide

- **Be explicit**: Explain everything, assume nothing
- **Show examples**: Include code snippets and diagrams
- **Highlight constraints**: Always document limitations
- **Explain decisions**: Why this approach, not alternatives?
- **Cross-reference**: Link to related documentation

---

## Getting Help

### Where to Look

1. **This index** - Find the right document
2. **Architecture overview** - Understand the big picture
3. **Specific component docs** - Deep dive into details
4. **Source code comments** - Inline documentation
5. **Design decisions** - Understand rationale

### Common Questions

**Q: How do I build MYRASPOS?**  
A: See [Build System](build/BUILD-SYSTEM.md)

**Q: How does the scheduler work?**  
A: See [Task Scheduling](kernel/03-TASK-SCHEDULING.md)

**Q: How is memory managed?**  
A: See [Memory Management](02-MEMORY-MANAGEMENT.md)

**Q: How do I create a GUI application?**  
A: See [Window Manager](kernel/wm/README.md) and [Applications](kernel/apps/README.md)

**Q: What shell commands are available?**  
A: See [Commands](kernel/commands/README.md)

**Q: Why was X designed this way?**  
A: See [Design Decisions](05-DESIGN-DECISIONS.md)

---

## Last Updated

2026-02-07

---

**Happy Exploring!** 🚀
