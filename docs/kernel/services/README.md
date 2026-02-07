# MYRASPOS Kernel Services Documentation

This directory contains comprehensive technical documentation for the core system services in MYRASPOS.

## Documentation Overview

### Service Components

1. **[shell.md](shell.md)** - Command Shell Service
   - Interactive command-line interface
   - Pipeline processing and job control
   - Built-in commands (cd, pwd, exit)
   - Path resolution and globbing
   - Integration with PTY for multi-session support

2. **[pty.md](pty.md)** - Pseudo-Terminal Service
   - Virtual terminal devices for multi-session support
   - Bidirectional buffered I/O
   - Line discipline and canonical mode
   - Ring buffer implementation
   - Integration with shell and I/O syscalls

3. **[service.md](service.md)** - Service Manager
   - Systemd-inspired service management
   - Unit file parsing and loading
   - Service lifecycle control (start/stop/restart)
   - Output redirection and logging
   - Integration with program registry and scheduler

4. **[programs.md](programs.md)** - Program Registry
   - Centralized command lookup system
   - Static program registration
   - Function pointer-based dispatch
   - Support for ~36 built-in programs
   - Filesystem, text processing, and system utilities

5. **[syscall.md](syscall.md)** - System Call Interface
   - Syscall registration and dispatch mechanism
   - 64 syscall slots with O(1) dispatch
   - I/O, filesystem, service, and timing syscalls
   - PTY-aware I/O redirection
   - Integration with all kernel subsystems

6. **[vectors.md](vectors.md)** - Exception Vectors (ARM64 Assembly)
   - Low-level exception handling infrastructure
   - 16-entry ARM64 exception vector table
   - Complete CPU context save/restore (272 bytes)
   - Exception routing to C handlers
   - Support for interrupts, syscalls, and faults

## Architecture Overview

```
┌─────────────────────────────────────────────────┐
│                  User Input                      │
│              (Console or PTY)                    │
└────────────────────┬────────────────────────────┘
                     │
                     ▼
          ┌──────────────────────┐
          │   Shell Service      │◄───────┐
          │   (shell.c/h)        │        │
          └──────────┬───────────┘        │
                     │                     │
                     │ Command             │ PTY I/O
                     ▼                     │
          ┌──────────────────────┐        │
          │  Program Registry     │        │
          │  (programs.c/h)       │        │
          └──────────┬───────────┘        │
                     │                     │
         ┌───────────┼───────────────┐    │
         │           │               │    │
         ▼           ▼               ▼    │
    ┌────────┐  ┌────────┐    ┌─────────┴──┐
    │FileOps │  │TextProc│    │ PTY Service │
    │Programs│  │Programs│    │ (pty.c/h)   │
    └────┬───┘  └────┬───┘    └─────────────┘
         │           │
         └─────┬─────┘
               │
               ▼
    ┌──────────────────────┐
    │  Syscall Interface    │
    │  (syscall.c/h)        │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────┐
    │  Exception Vectors    │
    │  (vectors.S)          │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────┐
    │  Kernel Subsystems    │
    │  (ramfs, sched, etc.) │
    └───────────────────────┘
```

## Service Interactions

### Shell → Program → Syscall Flow
```
User types: ls /home
    ↓
Shell parses command
    ↓
Shell looks up "ls" in Program Registry
    ↓
Program executes (prog_ls)
    ↓
Program calls syscall (SYS_RAMFS_LIST)
    ↓
Syscall handler accesses ramfs
    ↓
Results returned through chain
    ↓
Shell outputs to console/PTY
```

### Service Manager → Program Flow
```
systemctl start webserver
    ↓
Shell invokes prog_systemctl
    ↓
prog_systemctl calls SYS_SERVICE_START
    ↓
Service manager loads unit file
    ↓
Service manager parses ExecStart
    ↓
Service manager looks up program in registry
    ↓
Task created to run program
    ↓
Program output redirected to file (if specified)
```

### PTY → Shell → Output Flow
```
Client writes character to PTY input
    ↓
Shell reads via pty_getline()
    ↓
Shell executes command
    ↓
Command writes to output buffer
    ↓
Shell writes to PTY output
    ↓
Client reads from PTY output
    ↓
Client displays to terminal
```

## Key Design Principles

### 1. Modularity
Each service is independently implemented with clear interfaces:
- Shell knows about programs, not their implementations
- Programs use syscalls, not direct kernel APIs
- Services operate independently of shell

### 2. Uniform Interfaces
Standard function signatures throughout:
- Programs: `int (*)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap)`
- Syscalls: `uintptr_t (*)(uintptr_t a0, uintptr_t a1, uintptr_t a2)`
- Services: Standard start/stop/status operations

### 3. Static Registration
Compile-time registration for simplicity:
- Programs registered in static table
- Syscalls registered during init
- No dynamic loading complexity

### 4. PTY Transparency
PTY integration is transparent to programs:
- Syscalls automatically route to PTY if attached
- Programs use same I/O syscalls
- No PTY-specific code in programs

### 5. Pipeline Architecture
Data flows through buffers:
- Commands chain input → output
- Fixed-size buffers prevent unbounded memory
- Shell orchestrates pipeline execution

## Memory Management

### Stack Usage Per Service
- **Shell:** ~2KB buffer per command + pipeline overhead
- **PTY:** 2560 bytes (512 input + 2048 output buffers)
- **Service:** ~200 bytes per service entry + command strings
- **Programs:** Varies (typically <1KB for most operations)
- **Syscall:** Negligible (function pointers only)
- **Vectors:** 272 bytes per exception (temporary on stack)

### Heap Allocations
- **Shell:** Dynamic allocation for parsed commands and tokens
- **Service:** Single-block allocation for task arguments
- **Programs:** May allocate for file contents or processing
- **PTY:** Single allocation for pty structure

### Total Footprint (Typical System)
- Code: ~20-30KB for all services
- Data: ~5-10KB (tables, buffers, state)
- Stack: ~4KB per task × number of tasks
- Heap: Variable (depends on operations)

## Performance Characteristics

### Latency (Typical)
- **Command lookup:** 5-10μs (linear search through ~36 programs)
- **Syscall dispatch:** ~10ns (array lookup + function call)
- **Exception handling:** 40-50ns (context save/restore)
- **PTY I/O:** <1μs (ring buffer operations)
- **Service start:** ~100μs (task creation + setup)

### Throughput
- **Pipeline:** Limited by command execution, not overhead
- **PTY:** ~10KB/sec typical (buffer-limited)
- **Syscalls:** Millions per second (if no blocking operations)

## Thread Safety

### Thread-Safe Components
- **Program Registry:** Read-only after init
- **Syscall Table:** Read-only after registration
- **PTY I/O:** Interrupt-safe (using irq_save/restore)

### Not Thread-Safe
- **Shell:** Single-threaded design (per instance)
- **Service Manager:** No locking on service list
- **Programs:** Varies by implementation

### Concurrency Model
- Multiple PTY/shell instances can coexist
- Services run as independent tasks
- No shared mutable state between shells

## Error Handling Philosophy

### Graceful Degradation
- Commands write errors to output (not stderr yet)
- Shell continues after command errors
- Services can fail without system crash

### Error Reporting
- Negative return values indicate errors
- Error messages written to output buffers
- Critical errors logged to UART

### Recovery
- Shell survives command failures
- Service manager tracks failed services
- System remains operational after errors

## Future Enhancements

### Planned Features
1. **Userspace Separation:** Move programs to EL0
2. **Multi-user Support:** Per-user shells and permissions
3. **Advanced PTY:** VT100 emulation, more control codes
4. **Service Dependencies:** Before/After/Requires directives
5. **Dynamic Loading:** Load programs from filesystem
6. **Network Integration:** Network sockets for services
7. **Logging System:** Structured logging with levels
8. **Resource Limits:** Memory/CPU quotas per service

### API Stability
Current APIs are relatively stable, but may evolve:
- Syscall numbers may be extended (64 slots available)
- Program signature unlikely to change
- Service API may add new operations

## Documentation Standards

Each service document includes:
- **Overview:** Purpose and role in system
- **Data Structures:** Key types and their fields
- **Key Functions:** Complete API reference with examples
- **Implementation Details:** How it works internally
- **Design Decisions:** Rationale for architectural choices
- **Usage Examples:** Practical code examples
- **Cross-References:** Links to related documentation
- **Performance:** Memory, CPU, and latency characteristics
- **Thread Safety:** Concurrency guarantees and limitations

## Related Documentation

### Kernel Core
- `docs/kernel/memory/` - Memory management (kmalloc, palloc)
- `docs/kernel/fs/` - Filesystem (ramfs)
- `docs/kernel/sched/` - Task scheduler
- `docs/kernel/drivers/` - Device drivers (UART, timer)

### External References
- ARM Architecture Reference Manual (ARMv8)
- AAPCS64 (ARM 64-bit calling convention)
- Systemd documentation (for service unit format)

## Contributing

When adding new services or modifying existing ones:

1. **Update Documentation:** Keep docs in sync with code
2. **Follow Patterns:** Match existing service architecture
3. **Add Examples:** Include usage examples
4. **Document Decisions:** Explain design choices
5. **Cross-Reference:** Link to related services
6. **Test Thoroughly:** Verify all documented behavior

## Quick Reference

### Common Task: Adding a New Program
1. Implement function in `kernel/progs/myprogram.c`
2. Add declaration to `kernel/programs.h`
3. Register in `kernel/programs.c` table
4. Document in `docs/kernel/services/programs.md`
5. Rebuild kernel

### Common Task: Adding a New Syscall
1. Define syscall number in `kernel/syscall.h`
2. Implement handler in `kernel/syscall.c`
3. Register in `syscall_register_defaults()`
4. Document in `docs/kernel/services/syscall.md`
5. Rebuild kernel

### Common Task: Creating a New Service
1. Create unit file in ramfs `/etc/systemd/system/`
2. Set `ExecStart=` to existing program
3. Load via `service_load_unit()` or `services_load_all()`
4. Start via `service_start()` or systemctl
5. Verify with `service_status()` or systemctl status

## Version History

- **v0.2:** Current version - Full service infrastructure
  - Shell with pipeline support
  - PTY multi-session capability
  - Service manager with systemd-like units
  - 36+ programs in registry
  - 20+ syscalls implemented
  - Complete exception handling

- **v0.1:** Initial version - Basic shell and programs

## License

Documentation follows the same license as MYRASPOS source code.

---

*For questions or issues, refer to the main MYRASPOS repository.*
