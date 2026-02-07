# MYRASPOS I/O Subsystems Documentation

This directory contains comprehensive technical documentation for the MYRASPOS kernel I/O subsystems. Each document provides detailed information about hardware interfaces, implementation details, and usage examples.

## Documentation Files

### [uart.c.md](uart.c.md)
**PL011 UART Serial Driver**
- ARM PrimeCell UART (PL011) interface
- Serial console output and input
- Character I/O functions
- Kernel debugging and panic handling
- Memory-mapped I/O at 0x09000000

### [irq.c.md](irq.c.md)
**ARM GIC Interrupt Controller**
- Generic Interrupt Controller (GICv2) management
- Interrupt handler registration and dispatch
- Hardware interrupt configuration
- Critical section protection (irq_save/restore)
- Hybrid polling/interrupt model
- GIC Distributor (0x08000000) and CPU Interface (0x08010000)

### [timer.c.md](timer.c.md)
**ARM Generic Timer**
- ARMv8 architected timer subsystem
- Monotonic time tracking
- Task sleep/scheduling integration
- System register access (CNTPCT_EL0, CNTFRQ_EL0)
- Millisecond-resolution timing

### [framebuffer.c.md](framebuffer.c.md)
**Graphics Framebuffer Subsystem**
- 2D graphics rendering
- RGBA32 pixel format (32-bit color)
- Primitive drawing (rectangles, lines, pixels)
- Text rendering with built-in 5×7 font
- Bitmap drawing with alpha blending
- Terminal emulation
- VirtIO GPU integration

### [input.c.md](input.c.md)
**Keyboard and Mouse Input**
- Linux-compatible input event protocol
- Keyboard and mouse event queuing
- Mouse coordinate normalization
- Absolute (tablet) and relative (mouse) input
- Event type routing and filtering
- VirtIO input device integration

## Quick Reference

### Key Concepts

**Memory-Mapped I/O**: Direct hardware register access via memory addresses
- UART registers at 0x09000000
- GIC registers at 0x08000000 (Distributor) and 0x08010000 (CPU Interface)

**System Register Access**: ARMv8 MRS/MSR instructions for timer
- Read counter: `mrs x0, cntpct_el0`
- Read frequency: `mrs x0, cntfrq_el0`

**Interrupt Model**: Hybrid hardware/software approach
- Timer generates hardware interrupts (PPI)
- UART and VirtIO use polling in interrupt context
- GIC manages interrupt routing and acknowledgment

**Event-Driven I/O**: Asynchronous input handling
- Input events queued in ring buffers
- Tasks block waiting on event IDs
- Scheduler wakes tasks on input arrival

### Hardware Addresses (QEMU virt machine)

| Hardware | Base Address | Size | Description |
|----------|--------------|------|-------------|
| UART | 0x09000000 | 4KB | PL011 UART registers |
| GIC Distributor | 0x08000000 | 64KB | Interrupt distributor |
| GIC CPU Interface | 0x08010000 | 8KB | Per-CPU interrupt interface |
| Framebuffer | Variable | Variable | VirtIO GPU framebuffer |

### Common Data Structures

```c
// Input Event (input.h)
struct input_event {
    uint16_t type;     // Event type
    uint16_t code;     // Event code
    int32_t value;     // Event value
};

// Processor State (irq.h)
struct pt_regs {
    uint64_t regs[30]; // x0-x29
    uint64_t lr;       // x30
    uint64_t elr;      // Exception link register
    uint64_t spsr;     // Saved program status
};
```

### Typical Usage Patterns

**Early Boot Sequence**:
```c
irq_init();           // Initialize GIC and vectors
timer_init();         // Initialize timer frequency
uart_puts("Boot\n");  // Early console output
```

**Graphics Setup**:
```c
fb_init(addr, width, height, stride);
input_init(width, height);
fb_fill(0xFF000000);  // Clear to black
```

**Event Loop**:
```c
while (1) {
    timer_poll_and_advance();
    irq_poll_and_dispatch();
    
    struct input_event ev;
    while (input_pop_key_event(&ev)) {
        handle_input(ev);
    }
    
    task_schedule();
}
```

## Documentation Structure

Each document follows a consistent structure:

1. **Overview**: High-level description and purpose
2. **Hardware Details**: Register layout, memory addresses, protocols
3. **Key Functions**: Complete API reference with signatures
4. **Implementation Details**: Internal algorithms and data structures
5. **Design Decisions**: Rationale for implementation choices
6. **Constraints**: Hardware, software, and performance limitations
7. **Usage Examples**: Code samples showing typical usage
8. **Cross-References**: Links to related documentation and source files
9. **Thread Safety**: Concurrency considerations
10. **Performance**: Benchmarks and optimization notes
11. **Future Enhancements**: Possible improvements

## Related Documentation

### Kernel Subsystems
- `docs/kernel/sched/` - Task scheduling and context switching
- `docs/kernel/mm/` - Memory management (MMU, allocation)
- `docs/kernel/fs/` - File systems (ramfs, diskfs)

### Source Code
- `kernel/uart.c` / `kernel/uart.h`
- `kernel/irq.c` / `kernel/irq.h`
- `kernel/timer.c` / `kernel/timer.h`
- `kernel/framebuffer.c` / `kernel/framebuffer.h`
- `kernel/input.c` / `kernel/input.h`

## Architecture Notes

### Platform Target
- **Architecture**: ARMv8-A (AArch64)
- **Exception Level**: EL1 (kernel mode)
- **Platform**: QEMU `virt` machine
- **CPU**: Generic ARM Cortex-A series compatible
- **Endianness**: Little-endian

### Dependency Graph

```
Application Code
    ↓
Window Manager (wm.c)
    ↓
+-------------------+-------------------+
|                   |                   |
input.c        framebuffer.c        timer.c
    ↓                   ↓                ↓
virtio.c              virtio.c        sched.c
    ↓                   ↓                ↓
irq.c ←-----------------+----------------+
    ↓
uart.c
    ↓
Hardware (UART, GIC, Timer, VirtIO)
```

### Initialization Order

1. UART (earliest - needed for debug output)
2. IRQ (GIC setup, vector table)
3. Timer (frequency detection)
4. Memory management (MMU, allocators)
5. VirtIO (device discovery)
6. Framebuffer (graphics initialization)
7. Input (event system)
8. Scheduler (task management)
9. Window Manager (GUI)

## Development Guidelines

### Adding New I/O Drivers

When adding a new I/O driver:

1. **Follow existing patterns**:
   - Polling-based or interrupt-driven
   - Use appropriate locking (irq_save/restore)
   - Integrate with scheduler events

2. **Document thoroughly**:
   - Create corresponding .md file
   - Include hardware register details
   - Provide usage examples

3. **Consider portability**:
   - Abstract hardware addresses
   - Support multiple platforms if possible

4. **Test edge cases**:
   - Buffer overflows
   - Hardware timeouts
   - Concurrent access

### Code Style

- **Concise**: Minimal comments, self-documenting code
- **Performance**: Optimize hot paths (pixel operations, interrupt handlers)
- **Safety**: Bounds checking, null checks where needed
- **Clarity**: Prefer simple over clever

## References

### ARM Architecture References
- ARM Architecture Reference Manual ARMv8 (ARM ARM)
- ARM Generic Interrupt Controller Architecture Specification (GICv2)
- ARM PrimeCell UART (PL011) Technical Reference Manual
- ARM Generic Timer Architecture Specification

### Standards
- Linux Input Event Protocol
- VirtIO Specification 1.0+
- RGBA32 Pixel Format (standard 32-bit color)

### QEMU Documentation
- QEMU ARM `virt` Machine Documentation
- QEMU VirtIO Device Documentation

## Maintenance

This documentation should be updated when:
- Hardware interfaces change
- New functions added to APIs
- Performance characteristics change
- Bug fixes affect documented behavior
- Platform support expanded

---

**Last Updated**: 2024  
**MYRASPOS Version**: Development  
**Target Platform**: QEMU ARM virt machine (ARMv8-A)
