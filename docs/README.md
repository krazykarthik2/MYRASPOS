# MYRASPOS Documentation

This directory contains comprehensive documentation for the MYRASPOS operating system - a minimalist AArch64 micro-kernel designed for educational purposes and embedded systems.

## Documentation Structure

### 1. Architecture & Design
- [00-ARCHITECTURE.md](00-ARCHITECTURE.md) - Complete system architecture overview
- [01-BOOT-SEQUENCE.md](01-BOOT-SEQUENCE.md) - Boot process and initialization
- [02-MEMORY-MANAGEMENT.md](02-MEMORY-MANAGEMENT.md) - Memory subsystems (palloc, kmalloc, MMU)
- [kernel/03-TASK-SCHEDULING.md](kernel/03-TASK-SCHEDULING.md) - Task scheduler and context switching
- [kernel/services/vectors.md](kernel/services/vectors.md) - Exception vectors and IRQ handling
- [05-DESIGN-DECISIONS.md](05-DESIGN-DECISIONS.md) - Key design decisions and constraints

### 2. Build System
- [build/BUILD-SYSTEM.md](build/BUILD-SYSTEM.md) - Build system overview
- [build/sim.bat.md](build/sim.bat.md) - Simulator script
- [build/python-scripts.md](build/python-scripts.md) - Python utility scripts

### 3. Boot Subsystem
- [boot/start.s.md](boot/start.s.md) - Assembly boot code
- [boot/linker-files.md](boot/linker-files.md) - Linker scripts

### 4. Kernel Core
- [kernel/core/kernel.md](kernel/core/kernel.md) - Main kernel entry point
- [kernel/core/init.md](kernel/core/init.md) - Init task
- [kernel/03-TASK-SCHEDULING.md](kernel/03-TASK-SCHEDULING.md) - Task scheduler
- [kernel/core/swtch.md](kernel/core/swtch.md) - Context switching
- [kernel/services/syscall.md](kernel/services/syscall.md) - System call interface
- [kernel/services/vectors.md](kernel/services/vectors.md) - Exception vectors

### 5. Memory Management
- [kernel/memory/palloc.c.md](kernel/memory/palloc.c.md) - Page allocator
- [kernel/memory/kmalloc.c.md](kernel/memory/kmalloc.c.md) - Kernel heap allocator
- [kernel/memory/mmu.c.md](kernel/memory/mmu.c.md) - Memory management unit

### 6. I/O Subsystems
- [kernel/io/uart.c.md](kernel/io/uart.c.md) - Serial communication
- [kernel/io/framebuffer.c.md](kernel/io/framebuffer.c.md) - Graphics subsystem
- [kernel/io/input.c.md](kernel/io/input.c.md) - Input handling
- [kernel/io/timer.c.md](kernel/io/timer.c.md) - Timer subsystem
- [kernel/io/irq.c.md](kernel/io/irq.c.md) - Interrupt request handling

### 7. Filesystems
- [kernel/fs/ramfs.c.md](kernel/fs/ramfs.c.md) - RAM filesystem
- [kernel/fs/diskfs.c.md](kernel/fs/diskfs.c.md) - Disk filesystem
- [kernel/fs/files.c.md](kernel/fs/files.c.md) - File abstraction layer

### 8. Drivers
- [kernel/drivers/virtio.c.md](kernel/drivers/virtio.c.md) - VirtIO device drivers

### 9. System Services
- [kernel/services/pty.md](kernel/services/pty.md) - Pseudo-terminals
- [kernel/services/shell.md](kernel/services/shell.md) - Shell implementation
- [kernel/services/service.md](kernel/services/service.md) - Service manager
- [kernel/services/programs.md](kernel/services/programs.md) - Program management

### 10. Window Manager
- [kernel/wm/README.md](kernel/wm/README.md) - Window manager

### 11. Utility Libraries
- [kernel/lib/README.md](kernel/lib/README.md) - Standard library functions and helpers

### 12. Applications
- [kernel/apps/README.md](kernel/apps/README.md) - All bundled applications

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
