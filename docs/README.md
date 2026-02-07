# MYRASPOS Documentation

This directory contains comprehensive documentation for the MYRASPOS operating system - a minimalist AArch64 micro-kernel designed for educational purposes and embedded systems.

## Documentation Structure

### 1. Architecture & Design
- [00-ARCHITECTURE.md](00-ARCHITECTURE.md) - Complete system architecture overview
- [01-BOOT-SEQUENCE.md](01-BOOT-SEQUENCE.md) - Boot process and initialization
- [02-MEMORY-MANAGEMENT.md](02-MEMORY-MANAGEMENT.md) - Memory subsystems (palloc, kmalloc, MMU)
- [03-TASK-SCHEDULING.md](03-TASK-SCHEDULING.md) - Task scheduler and context switching
- [04-INTERRUPT-HANDLING.md](04-INTERRUPT-HANDLING.md) - Exception vectors and IRQ handling
- [05-DESIGN-DECISIONS.md](05-DESIGN-DECISIONS.md) - Key design decisions and constraints

### 2. Build System
- [build/BUILD-SYSTEM.md](build/BUILD-SYSTEM.md) - Build system overview
- [build/build.bat.md](build/build.bat.md) - Main build script
- [build/sim.bat.md](build/sim.bat.md) - Simulator script
- [build/python-scripts.md](build/python-scripts.md) - Python utility scripts

### 3. Boot Subsystem
- [boot/start.s.md](boot/start.s.md) - Assembly boot code
- [boot/linker-files.md](boot/linker-files.md) - Linker scripts

### 4. Kernel Core
- [kernel/kernel.md](kernel/kernel.md) - Main kernel entry point
- [kernel/init.md](kernel/init.md) - Init task
- [kernel/sched.md](kernel/sched.md) - Task scheduler
- [kernel/swtch.md](kernel/swtch.md) - Context switching
- [kernel/syscall.md](kernel/syscall.md) - System call interface
- [kernel/vectors.md](kernel/vectors.md) - Exception vectors

### 5. Memory Management
- [kernel/memory/palloc.md](kernel/memory/palloc.md) - Page allocator
- [kernel/memory/kmalloc.md](kernel/memory/kmalloc.md) - Kernel heap allocator
- [kernel/memory/mmu.md](kernel/memory/mmu.md) - Memory management unit

### 6. I/O Subsystems
- [kernel/io/uart.md](kernel/io/uart.md) - Serial communication
- [kernel/io/framebuffer.md](kernel/io/framebuffer.md) - Graphics subsystem
- [kernel/io/input.md](kernel/io/input.md) - Input handling
- [kernel/io/timer.md](kernel/io/timer.md) - Timer subsystem
- [kernel/io/irq.md](kernel/io/irq.md) - Interrupt request handling

### 7. Filesystems
- [kernel/fs/ramfs.md](kernel/fs/ramfs.md) - RAM filesystem
- [kernel/fs/diskfs.md](kernel/fs/diskfs.md) - Disk filesystem
- [kernel/fs/files.md](kernel/fs/files.md) - File abstraction layer

### 8. Drivers
- [kernel/drivers/virtio.md](kernel/drivers/virtio.md) - VirtIO device drivers

### 9. System Services
- [kernel/services/pty.md](kernel/services/pty.md) - Pseudo-terminals
- [kernel/services/shell.md](kernel/services/shell.md) - Shell implementation
- [kernel/services/service.md](kernel/services/service.md) - Service manager
- [kernel/services/programs.md](kernel/services/programs.md) - Program management

### 10. Window Manager
- [kernel/wm/wm.md](kernel/wm/wm.md) - Window manager
- [kernel/wm/cursor.md](kernel/wm/cursor.md) - Cursor handling

### 11. Utility Libraries
- [kernel/lib/lib.md](kernel/lib/lib.md) - Standard library functions
- [kernel/lib/glob.md](kernel/lib/glob.md) - Pattern matching
- [kernel/lib/image.md](kernel/lib/image.md) - Image handling
- [kernel/lib/lodepng.md](kernel/lib/lodepng.md) - PNG decoder
- [kernel/lib/panic.md](kernel/lib/panic.md) - Panic handler
- [kernel/lib/write.md](kernel/lib/write.md) - Write utilities

### 12. Applications
- [kernel/apps/calculator_app.md](kernel/apps/calculator_app.md) - Calculator application
- [kernel/apps/editor_app.md](kernel/apps/editor_app.md) - Text editor
- [kernel/apps/files_app.md](kernel/apps/files_app.md) - File manager
- [kernel/apps/image_viewer.md](kernel/apps/image_viewer.md) - Image viewer
- [kernel/apps/keyboard_tester_app.md](kernel/apps/keyboard_tester_app.md) - Keyboard tester
- [kernel/apps/myra_app.md](kernel/apps/myra_app.md) - Myra launcher application
- [kernel/apps/terminal_app.md](kernel/apps/terminal_app.md) - Terminal application

### 13. Shell Commands
- [kernel/commands/README.md](kernel/commands/README.md) - Commands overview
- Individual command documentation for all 24 commands

## Quick Start

1. **Understanding the System**: Start with [00-ARCHITECTURE.md](00-ARCHITECTURE.md)
2. **Building**: See [build/BUILD-SYSTEM.md](build/BUILD-SYSTEM.md)
3. **Running**: See [build/sim.bat.md](build/sim.bat.md)
4. **Developing**: Review design decisions in [05-DESIGN-DECISIONS.md](05-DESIGN-DECISIONS.md)

## Key Concepts

- **Preemptive Multitasking**: Round-robin scheduler with cooperative preemption
- **Memory Management**: Two-tier system (page allocator + heap allocator)
- **MMU**: 4-level page tables supporting 48-bit virtual addressing
- **I/O Model**: Polled I/O with interrupt capability
- **Filesystems**: Both RAM-based and disk-based storage
- **Graphics**: Framebuffer-based rendering with window manager

## Design Philosophy

MYRASPOS is designed as a pedagogical operating system that:
- Prioritizes code clarity over performance
- Demonstrates core OS concepts clearly
- Provides extensive debugging capabilities
- Uses simple, understandable algorithms
- Targets the AArch64 (ARM64) architecture

## Constraints and Limitations

- **Single-core only**: No SMP/multi-processor support
- **Cooperative scheduling**: Tasks must yield for fairness
- **Limited user space**: Basic user/kernel separation
- **No ASLR**: Fixed memory layout
- **Polled I/O**: Higher latency than interrupt-driven
- **Educational focus**: Not production-ready

## Contributing

When modifying MYRASPOS:
1. Understand the design constraints in [05-DESIGN-DECISIONS.md](05-DESIGN-DECISIONS.md)
2. Maintain code clarity and simplicity
3. Update relevant documentation
4. Test in QEMU simulation environment
5. Document all design decisions

## References

- ARM Architecture Reference Manual ARMv8
- Raspberry Pi 3 BCM2837 Technical Reference
- QEMU virt machine specification
- VirtIO specification

---

Last Updated: 2026-02-06
