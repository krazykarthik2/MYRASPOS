# System Call Interface Documentation

## Overview

The system call (syscall) interface (`syscall.c/h`) provides a software interface between kernel services and running tasks in MYRASPOS. It implements:
- Dynamic syscall registration mechanism
- Centralized syscall dispatch
- Standard syscall handlers for common operations
- Integration with filesystem, service manager, and I/O subsystems
- PTY-aware I/O redirection

System calls enable tasks to request kernel services in a controlled, uniform manner, providing abstraction over hardware and kernel internals.

## Data Structures

### `typedef uintptr_t (*syscall_fn)(uintptr_t, uintptr_t, uintptr_t)`
Function pointer type for syscall handlers.

**Parameters:**
- `a0`: First argument (typically pointer or value)
- `a1`: Second argument
- `a2`: Third argument

**Returns:** Result value or error code (uintptr_t).

**Design:** Fixed 3-argument interface for simplicity and performance.

**Casting:** Arguments and return values are cast to appropriate types within handlers.

### Syscall Table

```c
static syscall_fn table[SYSCALL_MAX];  // SYSCALL_MAX = 64
```

**Storage:** Static array of function pointers.

**Indexing:** Direct array access via syscall number (O(1) dispatch).

**Initialization:** All entries NULL until registered.

## Syscall Numbers

### I/O System Calls

```c
#define SYS_PUTS    1    // Print string to output
#define SYS_GETC    11   // Get character from input
```

### Filesystem System Calls

```c
#define SYS_RAMFS_CREATE            2    // Create file
#define SYS_RAMFS_WRITE             3    // Write to file
#define SYS_RAMFS_READ              4    // Read from file
#define SYS_RAMFS_REMOVE            5    // Remove file
#define SYS_RAMFS_MKDIR             6    // Create directory
#define SYS_RAMFS_LIST              7    // List directory contents
#define SYS_RAMFS_EXPORT            8    // Export filesystem
#define SYS_RAMFS_IMPORT            9    // Import filesystem
#define SYS_RAMFS_REMOVE_RECURSIVE  10   // Recursive remove
```

### Scheduling System Calls

```c
#define SYS_YIELD   12   // Yield CPU to scheduler
```

### Service Manager System Calls

```c
#define SYS_SERVICE_LOAD_ALL   16   // Load all service units
#define SYS_SERVICE_LOAD_UNIT  17   // Load specific unit
#define SYS_SERVICE_START      18   // Start service
#define SYS_SERVICE_STOP       19   // Stop service
#define SYS_SERVICE_RESTART    20   // Restart service
#define SYS_SERVICE_RELOAD     21   // Reload service config
#define SYS_SERVICE_ENABLE     22   // Enable service
#define SYS_SERVICE_DISABLE    23   // Disable service
#define SYS_SERVICE_STATUS     24   // Get service status
```

### Timing System Calls

```c
#define SYS_TIME    30   // Get monotonic time (ms)
#define SYS_SLEEP   31   // Sleep for milliseconds
```

## Key Functions

### Initialization and Registration

#### `void syscall_init(void)`
Initialize the syscall table (called once at boot).

**Behavior:**
- Zeroes all entries in syscall table
- Sets all function pointers to NULL

**Call Order:** Must be called before any syscall registration or handling.

**Example:**
```c
// In kernel_main()
syscall_init();
syscall_register_defaults();
```

#### `int syscall_register(uint32_t num, syscall_fn fn)`
Register a syscall handler for a specific syscall number.

**Parameters:**
- `num`: Syscall number (0-63)
- `fn`: Function pointer to handler

**Returns:**
- 0 on success
- -1 if `num >= SYSCALL_MAX`

**Usage:** Called by subsystems to register their syscall handlers.

**Example:**
```c
syscall_register(SYS_PUTS, sys_puts);
syscall_register(SYS_RAMFS_CREATE, sys_ramfs_create);
```

#### `void syscall_register_defaults(void)`
Register all default syscall handlers.

**Behavior:**
- Registers I/O syscalls (puts, getc)
- Registers filesystem syscalls
- Registers service manager syscalls
- Registers timing syscalls
- Registers scheduling syscalls

**Call Order:** Called after `syscall_init()` during kernel startup.

**Handlers Registered:** All standard syscalls listed in the syscall numbers section.

### Syscall Dispatch

#### `uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2)`
Dispatch a syscall to its registered handler.

**Parameters:**
- `num`: Syscall number to invoke
- `a0`, `a1`, `a2`: Arguments to pass to handler

**Returns:**
- Handler's return value on success
- `(uintptr_t)-1` if syscall not registered or invalid number

**Behavior:**
1. Validates syscall number (must be < SYSCALL_MAX)
2. Checks if handler registered (non-NULL)
3. Calls handler with arguments
4. Returns handler result

**Thread Safety:** Safe if handlers are thread-safe.

**Example:**
```c
// Direct call (from exception handler)
uintptr_t result = syscall_handle(SYS_PUTS, 
                                   (uintptr_t)"Hello\n", 
                                   0, 0);
```

## Syscall Handlers

### I/O Handlers

#### `sys_puts(a0, a1, a2)`
Print a string to output (console or PTY).

**Parameters:**
- `a0`: `const char *` - String to print

**Behavior:**
1. Checks if current task has PTY attached (`task_get_tty()`)
2. If PTY: Writes characters to PTY output buffer
3. If no PTY: Writes to UART console

**PTY Integration:** Automatically redirects output to task's PTY if available.

**Example:**
```c
syscall(SYS_PUTS, "Hello, World!\n", 0, 0);
```

#### `sys_getc(a0, a1, a2)`
Read a character from input (console or PTY).

**Parameters:** None used

**Returns:** Character as `uintptr_t`, or 0 if no input available.

**Behavior:**
1. Checks if current task has PTY attached
2. If PTY: Reads from PTY input buffer (`pty_read_in()`)
3. If no PTY: Reads from UART console (`uart_getc()`)

**Non-blocking:** Returns 0 immediately if no input available.

**Example:**
```c
char c = (char)syscall(SYS_GETC, 0, 0, 0);
```

### Filesystem Handlers

#### `sys_ramfs_create(a0, a1, a2)`
Create a new file in ramfs.

**Parameters:**
- `a0`: `const char *` - File path

**Returns:** 0 on success, negative on error.

**Example:**
```c
syscall(SYS_RAMFS_CREATE, "/tmp/newfile.txt", 0, 0);
```

#### `sys_ramfs_write(a0, a1, a2)`
Write data to a file.

**Parameters:**
- `a0`: `const char *` - File path
- `a1`: `const void *` - Data buffer
- `a2`: `size_t` - Number of bytes to write

**Returns:** Number of bytes written, or negative on error.

**Example:**
```c
const char *data = "Hello, File!";
syscall(SYS_RAMFS_WRITE, "/tmp/test.txt", data, strlen(data));
```

#### `sys_ramfs_read(a0, a1, a2)`
Read data from a file.

**Parameters:**
- `a0`: `const char *` - File path
- `a1`: `void *` - Destination buffer
- `a2`: `size_t` - Buffer size

**Returns:** Number of bytes read, or negative on error.

**Example:**
```c
char buffer[256];
int len = syscall(SYS_RAMFS_READ, "/tmp/test.txt", buffer, sizeof(buffer));
```

#### `sys_ramfs_remove(a0, a1, a2)`
Remove a file.

**Parameters:**
- `a0`: `const char *` - File path

**Returns:** 0 on success, negative on error.

#### `sys_ramfs_mkdir(a0, a1, a2)`
Create a directory.

**Parameters:**
- `a0`: `const char *` - Directory path

**Returns:** 0 on success, negative on error.

#### `sys_ramfs_list(a0, a1, a2)`
List directory contents.

**Parameters:**
- `a0`: `const char *` - Directory path
- `a1`: `char *` - Output buffer
- `a2`: `size_t` - Buffer size

**Returns:** Number of bytes written, or negative on error.

**Format:** Entries separated by newlines, terminated by null.

#### `sys_ramfs_export(a0, a1, a2)`
Export filesystem to disk.

**Parameters:**
- `a0`: `const char *` - Export path

**Returns:** 0 on success, negative on error.

#### `sys_ramfs_import(a0, a1, a2)`
Import filesystem from disk.

**Parameters:**
- `a0`: `const char *` - Import path

**Returns:** 0 on success, negative on error.

#### `sys_ramfs_remove_recursive(a0, a1, a2)`
Recursively remove directory and contents.

**Parameters:**
- `a0`: `const char *` - Directory path

**Returns:** 0 on success, negative on error.

### Service Manager Handlers

#### `sys_service_load_all(a0, a1, a2)`
Load all service units from `/etc/systemd/system`.

**Returns:** 0 on success, negative on error.

#### `sys_service_load_unit(a0, a1, a2)`
Load a specific service unit file.

**Parameters:**
- `a0`: `const char *` - Unit file path

**Returns:** 0 on success, negative on error.

#### `sys_service_start(a0, a1, a2)`
Start a service.

**Parameters:**
- `a0`: `const char *` - Service name

**Returns:** Process ID on success, negative on error.

#### `sys_service_stop(a0, a1, a2)`
Stop a service.

**Parameters:**
- `a0`: `const char *` - Service name

**Returns:** 0 on success, negative on error.

#### `sys_service_restart(a0, a1, a2)`
Restart a service.

**Parameters:**
- `a0`: `const char *` - Service name

**Returns:** 0 on success, negative on error.

#### `sys_service_reload(a0, a1, a2)`
Reload service configuration.

**Parameters:**
- `a0`: `const char *` - Service name (NULL for all)

**Returns:** 0 on success, negative on error.

#### `sys_service_enable(a0, a1, a2)`
Enable a service.

**Parameters:**
- `a0`: `const char *` - Service name

**Returns:** 0 on success, negative on error.

#### `sys_service_disable(a0, a1, a2)`
Disable a service.

**Parameters:**
- `a0`: `const char *` - Service name

**Returns:** 0 on success, negative on error.

#### `sys_service_status(a0, a1, a2)`
Get service status.

**Parameters:**
- `a0`: `const char *` - Service name
- `a1`: `char *` - Output buffer
- `a2`: `size_t` - Buffer size

**Returns:** Number of bytes written, or negative on error.

### Timing Handlers

#### `sys_time(a0, a1, a2)`
Get current monotonic time.

**Returns:** Time in milliseconds since boot.

**Example:**
```c
uint64_t start = syscall(SYS_TIME, 0, 0, 0);
// ... do work ...
uint64_t end = syscall(SYS_TIME, 0, 0, 0);
uart_put_hex(end - start);  // Print elapsed ms
```

#### `sys_sleep(a0, a1, a2)`
Sleep for specified milliseconds.

**Parameters:**
- `a0`: `uint32_t` - Milliseconds to sleep

**Behavior:** Blocks current task for specified duration.

**Example:**
```c
syscall(SYS_SLEEP, 1000, 0, 0);  // Sleep 1 second
```

### Scheduling Handlers

#### `sys_yield(a0, a1, a2)`
Voluntarily yield CPU to scheduler.

**Behavior:** Calls `schedule()` to give other tasks a chance to run.

**Returns:** 0

**Use Case:** Cooperative multitasking, busy-wait loops.

**Example:**
```c
while (!data_ready()) {
    syscall(SYS_YIELD, 0, 0, 0);
}
```

## Implementation Details

### Syscall Table Structure

```c
#define SYSCALL_MAX 64
static syscall_fn table[SYSCALL_MAX];
```

**Design:**
- Fixed-size array (64 entries)
- Direct indexing (O(1) dispatch)
- NULL indicates unregistered syscall

**Memory:** 64 × 8 bytes = 512 bytes (function pointers).

### Dispatch Mechanism

```c
uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    if (num >= SYSCALL_MAX || !table[num]) 
        return (uintptr_t)-1;
    return table[num](a0, a1, a2);
}
```

**Validation:**
1. Check syscall number in range
2. Check handler registered

**Performance:** ~3 instructions + function call overhead.

### PTY-Aware I/O

I/O syscalls automatically route to PTY if attached:

```c
struct pty *p = task_get_tty(task_current_id());
if (p) {
    // Use PTY functions
    pty_write_out(p, c);
} else {
    // Use console
    uart_puts(s);
}
```

**Benefit:** Transparent redirection without task knowledge.

### Argument Passing Convention

**Register Mapping (ARM64):**
- Syscall number: Typically in `x0` or immediate
- Arguments: `x0, x1, x2` (first 3 general-purpose registers)
- Return: `x0`

**C Interface:**
```c
uintptr_t syscall(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2);
```

### Error Handling

**Convention:**
- Success: Return value >= 0
- Error: Return value < 0 (typically -1)

**Invalid Syscall:**
```c
if (num >= SYSCALL_MAX || !table[num])
    return (uintptr_t)-1;
```

**Handler Errors:** Up to individual handlers to return appropriate codes.

## Design Decisions

### Why Fixed 3-Argument Interface?
**Decision:** All syscalls take exactly 3 arguments.

**Rationale:**
- Simplifies dispatch code
- Matches ARM64 calling convention efficiently
- Sufficient for most operations
- Unused arguments ignored (no overhead)

**Limitation:** Complex operations may need structures passed by pointer.

### Why Static Table?
**Decision:** Use static array instead of dynamic registration.

**Rationale:**
- O(1) dispatch performance
- Simple implementation
- Predictable memory usage
- No fragmentation

**Trade-off:** Fixed maximum (64 syscalls), sufficient for current needs.

### Why Separate Registration Function?
**Decision:** Provide `syscall_register()` for dynamic handler setup.

**Rationale:**
- Decouples syscall numbers from implementations
- Allows subsystems to register independently
- Enables conditional compilation (register only if compiled in)
- Simplifies testing (can replace handlers)

### Why PTY-Aware I/O?
**Decision:** Check for PTY attachment in I/O handlers.

**Rationale:**
- Transparent multi-session support
- Programs don't need PTY-aware code
- Centralized I/O routing logic
- Maintains single I/O syscall interface

**Alternative:** Separate syscalls for PTY I/O would complicate programs.

### Why Three Arguments Only?
**Decision:** Limit to 3 arguments (a0, a1, a2).

**Rationale:**
- Maps cleanly to register convention (x0-x2)
- Sufficient for typical operations
- Complex data via pointers to structures
- Simplifies exception handler assembly

**Linux Comparison:** Linux syscalls support up to 6 arguments.

### Why Direct Function Pointers?
**Decision:** Store raw function pointers, not wrapped objects.

**Rationale:**
- Minimal overhead (direct call)
- No indirection penalty
- Simple implementation
- Type safety via typedef

## Usage Examples

### Invoking Syscalls from C Code

```c
// Print to console/PTY
syscall(SYS_PUTS, "Hello from syscall\n", 0, 0);

// Create and write file
syscall(SYS_RAMFS_CREATE, "/tmp/test.txt", 0, 0);
syscall(SYS_RAMFS_WRITE, "/tmp/test.txt", "data", 4);

// Read file
char buf[256];
int len = syscall(SYS_RAMFS_READ, "/tmp/test.txt", buf, sizeof(buf));

// Start service
int pid = syscall(SYS_SERVICE_START, "nginx", 0, 0);

// Get time
uint64_t now = syscall(SYS_TIME, 0, 0, 0);

// Sleep
syscall(SYS_SLEEP, 500, 0, 0);  // 500ms

// Yield CPU
syscall(SYS_YIELD, 0, 0, 0);
```

### Registering Custom Syscall

**Step 1:** Define handler:
```c
static uintptr_t my_custom_syscall(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    // Custom logic
    uart_puts("Custom syscall invoked\n");
    return 0;
}
```

**Step 2:** Allocate syscall number:
```c
#define SYS_CUSTOM  50  // Unused number
```

**Step 3:** Register:
```c
syscall_register(SYS_CUSTOM, my_custom_syscall);
```

**Step 4:** Invoke:
```c
syscall(SYS_CUSTOM, arg1, arg2, arg3);
```

### Error Handling Pattern

```c
int result = syscall(SYS_RAMFS_CREATE, "/tmp/file", 0, 0);
if (result < 0) {
    uart_puts("Error: Failed to create file\n");
    return -1;
}
```

### PTY I/O Example

```c
// Task with PTY attached
void task_with_pty(void *arg) {
    // Output automatically goes to PTY
    syscall(SYS_PUTS, "Output to PTY\n", 0, 0);
    
    // Input comes from PTY
    char c;
    while ((c = syscall(SYS_GETC, 0, 0, 0)) != 0) {
        // Process input
    }
}
```

### Service Management Example

```c
// Load services
syscall(SYS_SERVICE_LOAD_ALL, 0, 0, 0);

// Start web server
int pid = syscall(SYS_SERVICE_START, "webserver", 0, 0);
if (pid > 0) {
    uart_puts("Webserver started\n");
}

// Check status
char status[512];
syscall(SYS_SERVICE_STATUS, "webserver", status, sizeof(status));
uart_puts(status);

// Stop service
syscall(SYS_SERVICE_STOP, "webserver", 0, 0);
```

## Cross-References

### Related Services
- **[pty.md](pty.md)** - PTY integration for I/O redirection
- **[service.md](service.md)** - Service manager syscall handlers
- **[shell.md](shell.md)** - Uses syscalls indirectly via kernel APIs
- **[programs.md](programs.md)** - Programs invoke syscalls

### Related Components
- **ramfs.h/c** - Filesystem operations
- **sched.h/c** - Scheduler (yield, sleep)
- **timer.h/c** - Timing operations
- **uart.h/c** - Console I/O
- **init.h/c** - Task TTY management (`task_get_tty()`)

### Exception Handling
- **[vectors.md](vectors.md)** - Exception vectors (SVC handler invokes `syscall_handle()`)
- **exception.c** - Exception dispatcher

### Invocation Chain

```
User Code
    ↓ (SVC instruction)
Exception Vector (vectors.S)
    ↓
Exception Handler (exception.c)
    ↓ (extracts syscall number and args)
syscall_handle()
    ↓ (dispatch to handler)
Syscall Handler (sys_*)
    ↓ (call kernel service)
Return to User
```

## Thread Safety

**Syscall Dispatch:** Thread-safe (read-only table access).

**Handler Safety:** Depends on individual handlers:
- **ramfs handlers:** Not thread-safe (no locking)
- **service handlers:** Not thread-safe
- **I/O handlers:** Interrupt-safe (PTY uses irq_save/restore)
- **timing handlers:** Thread-safe

**Recommendation:** Avoid concurrent syscalls modifying shared state.

## Performance Considerations

### Dispatch Overhead
- Table lookup: ~1 cycle (array index)
- Validation: ~2 cycles (2 comparisons)
- Function call: ~3-5 cycles
- **Total:** ~6-8 cycles (~10-15ns @ 1GHz)

### Memory Footprint
- Syscall table: 512 bytes (64 × 8-byte pointers)
- Handler code: ~2-10KB total
- **Total:** ~3KB

### Syscall Latency
- Dispatch: ~10ns
- Context switch (if needed): ~1-2μs
- Handler execution: Varies (μs to ms)

**Typical:** Filesystem syscalls dominate (μs range for ramfs operations).

### Optimization Opportunities
- Inline hot syscalls (puts, getc)
- Batch operations to reduce syscall count
- Async I/O for non-blocking operations

## Error Codes

### Standard Returns
- **Success:** >= 0 (often byte count or PID)
- **Error:** < 0 (typically -1)

### Specific Error Cases
- **Invalid syscall:** `-1` from `syscall_handle()`
- **Not found:** `-1` from filesystem operations
- **Permission:** Not implemented yet
- **No memory:** Varies by operation

**Future:** Implement errno-style error codes (ENOENT, ENOMEM, etc.).

## Future Enhancements

### Planned Features
1. **Extended arguments:** Support for syscalls with >3 arguments via structure
2. **Error codes:** Proper errno-style error reporting
3. **Permissions:** Per-syscall permission checks
4. **Async syscalls:** Non-blocking I/O operations
5. **Syscall tracing:** Debug/profiling infrastructure
6. **User/kernel separation:** When switching to privileged/unprivileged model

### API Extensions
```c
// Future syscall structure for complex args
struct syscall_args {
    uint32_t num;
    uintptr_t args[8];  // Extended argument count
};

// Async syscall support
int syscall_async(uint32_t num, struct syscall_args *args, 
                  void (*callback)(uintptr_t result));
```

### Userspace Integration
When MYRASPOS adds userspace:
- SVC instruction will trap to EL1
- Exception handler extracts syscall info from registers
- Current implementation stays mostly intact
- Additional security checks added
