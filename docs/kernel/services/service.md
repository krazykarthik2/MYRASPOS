# Service Manager Documentation

## Overview

The service manager (`service.c/h`) implements a systemd-inspired service management system for MYRASPOS. It provides:
- Service unit file parsing and loading
- Service lifecycle management (start, stop, restart)
- Service state tracking (enabled, running, inactive)
- Output redirection for service logs
- Integration with the program registry and task scheduler
- Persistent service configuration via ramfs

The service manager enables background daemons, system services, and automated task execution, providing a foundation for a modular, extensible system architecture.

## Data Structures

### `struct service_entry`
Internal representation of a service unit.

```c
struct service_entry {
    char name[SRV_NAME_MAX];    // Service short name (without .service)
    char *unit_path;             // Full path to unit file in ramfs
    char *exec;                  // ExecStart command line
    char *redir_target;          // Output redirection target file
    int redir_append;            // 1 for >>, 0 for > (append vs overwrite)
    int enabled;                 // 1 if enabled, 0 if disabled
    int pid;                     // Task ID of running service (0 if stopped)
    struct service_entry *next;  // Linked list next pointer
};
```

**Fields:**
- `name`: Service identifier (e.g., "webserver" from "webserver.service")
- `unit_path`: Location of unit file (e.g., "/etc/systemd/system/webserver.service")
- `exec`: Command to execute (parsed from `ExecStart=` directive)
- `redir_target`: File path for stdout redirection (parsed from `>` or `>>`)
- `redir_append`: Append mode flag for redirection
- `enabled`: Persistent enable/disable state (for future auto-start)
- `pid`: Current task ID if service is running, 0 otherwise
- `next`: Pointer to next service in linked list

### `struct svc_task_arg`
Arguments passed to service task wrapper.

```c
struct svc_task_arg {
    char **argv;                  // Argument vector (points into mem)
    int argc;                     // Argument count
    struct service_entry *svc;    // Pointer to service entry
    void *mem;                    // Single memory block for cleanup
};
```

**Memory Layout:**
The `mem` block contains a packed layout:
```
[struct svc_task_arg][argv pointers][command string]
```
This design enables single-allocation and single-free for efficiency.

### Constants

```c
#define SRV_NAME_MAX 64    // Maximum service name length
```

## Key Functions

### Initialization

#### `int services_init(void)`
Initialize the service manager subsystem.

**Behavior:**
1. Creates directory hierarchy:
   - `/etc`
   - `/etc/systemd`
   - `/etc/systemd/system`
2. Initializes empty service list

**Returns:** 0 on success, -1 on error.

**Call Order:** Should be called during kernel initialization, before `services_load_all()`.

**Example:**
```c
// In kernel init
services_init();
```

### Loading Services

#### `int service_load_unit(const char *path)`
Load a single service unit file from ramfs.

**Parameters:**
- `path`: Absolute path to `.service` file (e.g., "/etc/systemd/system/nginx.service")

**Behavior:**
1. Reads unit file from ramfs
2. Parses `ExecStart=` directive
3. Parses optional output redirection (`>`, `>>`)
4. Derives service name from filename (removes `.service` extension)
5. Updates existing service or creates new entry

**Unit File Format:**
```ini
[Service]
ExecStart=/path/to/program [args] [> /output/file]
```

**Returns:** 0 on success, -1 on error (file not found, parse error).

**Example:**
```c
// Load specific service
service_load_unit("/etc/systemd/system/myapp.service");
```

#### `int services_load_all(void)`
Scan and load all service units from the system directory.

**Behavior:**
1. Lists all files in `/etc/systemd/system`
2. For each `.service` file, calls `service_load_unit()`
3. Updates service registry

**Returns:** 0 on success, -1 if directory doesn't exist.

**Use Case:** Called during system initialization or after adding new services.

**Example:**
```c
// Load all services at boot
services_load_all();
```

### Service Control

#### `int service_start(const char *name)`
Start a service by name.

**Parameters:**
- `name`: Service name (without `.service` extension)

**Behavior:**
1. Looks up service in registry
2. Checks if already running (returns 0 if yes)
3. Parses `exec` command into argv
4. Allocates packed memory block (struct + argv + strings)
5. Creates new task with `service_task_fn` wrapper
6. Stores task PID in service entry

**Task Execution:**
- Service runs in its own task context
- Output redirected to file if specified
- Task automatically terminates after program completes
- Task disables itself via `task_set_fn_null()`

**Returns:** 
- Task PID on success
- 0 if already running
- -1 on error (service not found, OOM, task creation failed)

**Example:**
```c
// Start nginx service
int pid = service_start("nginx");
if (pid > 0) {
    uart_puts("nginx started\n");
}
```

#### `int service_stop(const char *name)`
Stop a running service.

**Parameters:**
- `name`: Service name

**Behavior:**
1. Looks up service
2. Checks if running (returns 0 if already stopped)
3. Kills task via `task_kill()`
4. Clears PID field in service entry

**Returns:** 0 on success, -1 if service not found.

**Note:** Immediate termination (no graceful shutdown).

**Example:**
```c
service_stop("nginx");
```

#### `int service_restart(const char *name)`
Restart a service (stop then start).

**Parameters:**
- `name`: Service name

**Behavior:**
1. Calls `service_stop(name)`
2. Calls `service_start(name)`

**Returns:** Result of `service_start()`.

**Use Case:** Apply configuration changes that require restart.

**Example:**
```c
// Restart after config change
service_restart("nginx");
```

#### `int service_reload(const char *name)`
Reload a service unit file.

**Parameters:**
- `name`: Service name, or NULL to reload all services

**Behavior:**
- **NULL name:** Calls `services_load_all()` to reload all units
- **Specific name:** 
  1. Reloads unit file from disk
  2. If service is running, restarts it to apply changes
  3. If service not found, attempts to load from standard path

**Returns:** 0 on success, -1 on error.

**Use Case:** Apply unit file changes without reboot.

**Example:**
```c
// Reload specific service
service_reload("nginx");

// Reload all services
service_reload(NULL);
```

### Service State Management

#### `int service_enable(const char *name)`
Mark a service as enabled (for future auto-start support).

**Parameters:**
- `name`: Service name

**Behavior:**
1. Looks up or loads service
2. Sets `enabled` flag to 1

**Returns:** 0 on success, -1 if service not found or can't be loaded.

**Note:** Currently only sets flag. Auto-start on boot not yet implemented.

**Example:**
```c
service_enable("nginx");
```

#### `int service_disable(const char *name)`
Mark a service as disabled.

**Parameters:**
- `name`: Service name

**Behavior:**
1. Looks up or loads service
2. Sets `enabled` flag to 0

**Returns:** 0 on success, -1 if service not found.

**Example:**
```c
service_disable("nginx");
```

#### `int service_status(const char *name, char *buf, size_t len)`
Get service status information.

**Parameters:**
- `name`: Service name
- `buf`: Output buffer
- `len`: Buffer size

**Returns:** Number of bytes written, or -1 if service not found.

**Output Format:**
```
Name: service_name
Unit: /etc/systemd/system/service_name.service
Exec: /path/to/program args
Enabled: yes/no
Active: running/inactive
```

**Example:**
```c
char status[256];
service_status("nginx", status, sizeof(status));
uart_puts(status);
```

### Internal Functions

#### `static struct service_entry *find_service(const char *name)`
Look up a service by name in the registry.

**Returns:** Pointer to service entry, or NULL if not found.

#### `static void service_task_fn(void *arg)`
Task wrapper function for service execution.

**Parameters:**
- `arg`: Pointer to `struct svc_task_arg`

**Behavior:**
1. Extracts argv and service info
2. Looks up program in registry
3. Executes program (captures output)
4. Handles output redirection:
   - If `redir_target` specified: Writes to file
   - Otherwise: Prints to console
5. Marks service as stopped (clears PID)
6. Disables task via `task_set_fn_null()`
7. Frees memory block

**Fallback:** Implements simple `echo` command if program not in registry.

## Implementation Details

### Unit File Parsing

The parser is minimal and focuses on the `ExecStart=` directive:

**Algorithm:**
1. Read entire file into buffer
2. Search for "ExecStart=" string
3. Skip leading whitespace after `=`
4. Copy command line until newline
5. Parse redirection operators (`>`, `>>`) if present
6. Split redirection: command before `>`, target after

**Limitations:**
- Only `ExecStart=` is parsed (other directives ignored)
- No variable expansion or quoting
- No environment file support
- No dependency handling (Before=, After=, etc.)

**Example Unit:**
```ini
[Unit]
Description=My Web Server

[Service]
ExecStart=/usr/bin/webserver --port 8080 > /var/log/webserver.log

[Install]
WantedBy=multi-user.target
```
Parsed result:
- `exec` = "/usr/bin/webserver --port 8080"
- `redir_target` = "/var/log/webserver.log"
- `redir_append` = 0

### Command Line Parsing

Service commands are parsed into argv:

**Algorithm:**
1. Count tokens (whitespace-separated)
2. Allocate single memory block:
   - `struct svc_task_arg`
   - Array of `char*` pointers (argv)
   - String buffer for command
3. Copy command into string buffer
4. Split on spaces in-place (null-terminate tokens)
5. Set argv pointers to token starts

**Memory Efficiency:** Single allocation/free reduces fragmentation.

### Output Redirection

When `redir_target` is specified:

**Behavior:**
1. Ensure parent directory exists (`ramfs_mkdir()`)
2. Remove old file if exists (`ramfs_remove()`)
3. Create new file (`ramfs_create()`)
4. Write program output to file (`ramfs_write()`)

**File Creation:**
- Overwrite mode (`>`): Old file removed before write
- Append mode (`>>`): Flag set but currently overwrites (TODO: append support)

**Relative Paths:** Converted to absolute by prefixing `/` if needed.

### Service Registry

Services are stored in a global linked list:

**Structure:**
```
services (global) → [entry1] → [entry2] → [entry3] → NULL
```

**Lookup:** Linear search by name (O(n)).

**Lifetime:** Service entries persist until kernel shutdown (no removal).

### Task Integration

Services run as independent tasks:

**Lifecycle:**
1. `service_start()` creates task with `task_create()`
2. Task runs `service_task_fn()` wrapper
3. Wrapper executes program via `program_lookup()`
4. After completion, wrapper calls `task_set_fn_null()`
5. Task remains in scheduler but never runs again

**State Tracking:**
- `pid` field tracks task ID
- `pid == 0` means service stopped
- `pid > 0` means service running

**Cleanup:** Task memory freed by wrapper before exit.

### Silent Service Execution

Services with redirection suppress console output:

**Detection:**
```c
if (!a->svc || !a->svc->redir_target) {
    uart_puts("[svc] log message\n");
}
```

**Rationale:** Prevents console clutter from background services.

## Design Decisions

### Why Systemd-Inspired?
**Decision:** Model after systemd unit files and systemctl commands.

**Rationale:**
- Familiar interface for Linux users
- Proven design for service management
- Clear separation of configuration and code
- Enables declarative service definitions

**Simplification:** Only subset of systemd features implemented (no dependencies, no complex unit types).

### Why Single ExecStart?
**Decision:** Parse only `ExecStart=` directive, ignore all other fields.

**Rationale:**
- Sufficient for basic service execution
- Keeps parser simple and robust
- Most critical directive for service function
- Additional directives can be added incrementally

**Trade-off:** No support for pre/post scripts, dependencies, or advanced features.

### Why Linked List Registry?
**Decision:** Store services in a simple linked list.

**Rationale:**
- Expected number of services: < 20
- Linear search overhead negligible
- Simple implementation, easy to debug
- No need for hash table complexity

**Performance:** O(n) lookup acceptable for small n.

### Why Single Memory Block for Task Args?
**Decision:** Pack struct, argv, and strings into one allocation.

**Rationale:**
- Reduces fragmentation (one alloc vs. many small allocs)
- Simplifies cleanup (one free vs. tracking many)
- Prevents memory leaks in error paths
- Improves cache locality

**Implementation Complexity:** Slightly more complex allocation logic, but worth the benefits.

### Why No Graceful Shutdown?
**Decision:** Use `task_kill()` for immediate termination.

**Rationale:**
- Simpler implementation (no signal handling)
- Sufficient for current use cases
- Services can be designed to be restart-safe
- Graceful shutdown can be added later if needed

**Limitation:** Services can't clean up resources on stop.

### Why No Automatic Restart?
**Decision:** Services don't restart automatically after failure.

**Rationale:**
- Prevents infinite crash loops
- Requires explicit restart by administrator
- Simpler state management
- Future enhancement: Add `Restart=` directive

### Why Output Redirection in Unit File?
**Decision:** Support `> file` and `>> file` in `ExecStart=`.

**Rationale:**
- Convenient for logging services
- Avoids need for separate log management
- Familiar shell-like syntax
- Keeps service output contained

**Alternative:** Could use separate log facility, but redirection is simpler.

## Usage Examples

### Creating a Service Unit

**File:** `/etc/systemd/system/webserver.service`
```ini
[Service]
ExecStart=webserver --port 8080 > /var/log/web.log
```

**Create via ramfs:**
```c
const char *content = "[Service]\nExecStart=webserver --port 8080 > /var/log/web.log\n";
ramfs_create("/etc/systemd/system/webserver.service");
ramfs_write("/etc/systemd/system/webserver.service", content, strlen(content), 0);
```

### Loading and Starting a Service
```c
// Initialize service manager
services_init();

// Load specific service
service_load_unit("/etc/systemd/system/webserver.service");

// Start the service
int pid = service_start("webserver");
if (pid > 0) {
    uart_puts("Webserver started with PID: ");
    uart_put_hex(pid);
    uart_puts("\n");
}
```

### Managing Service Lifecycle
```c
// Start service
service_start("myapp");

// Check status
char status[512];
service_status("myapp", status, sizeof(status));
uart_puts(status);

// Restart service
service_restart("myapp");

// Stop service
service_stop("myapp");
```

### Loading All Services at Boot
```c
void system_init(void) {
    // ... other init ...
    
    services_init();
    services_load_all();
    
    // Optionally start enabled services
    // (auto-start not yet implemented)
}
```

### Service with Complex Command
```ini
[Service]
ExecStart=log-viewer --file /var/log/system.log --tail 100 > /tmp/recent.log
```

### Echo Service (Fallback Example)
```ini
[Service]
ExecStart=echo Hello from service! > /tmp/greeting.txt
```

### Checking Service State
```c
void monitor_services(void) {
    const char *services[] = {"nginx", "mysql", "redis", NULL};
    
    for (int i = 0; services[i]; ++i) {
        char buf[256];
        int ret = service_status(services[i], buf, sizeof(buf));
        if (ret > 0) {
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}
```

### Reloading Service Configuration
```c
// Modify unit file
ramfs_remove("/etc/systemd/system/myapp.service");
ramfs_create("/etc/systemd/system/myapp.service");
ramfs_write("/etc/systemd/system/myapp.service", new_config, len, 0);

// Reload and restart
service_reload("myapp");  // Automatically restarts if running
```

## Cross-References

### Related Services
- **[programs.md](programs.md)** - Program registry for service executables
- **[shell.md](shell.md)** - Shell can invoke systemctl for service control
- **[syscall.md](syscall.md)** - Syscall interface exposes service operations
- **[pty.md](pty.md)** - Services can use PTY for interactive I/O (future)

### Related Kernel Components
- **sched.h/c** - Task scheduler for service execution
  - `task_create()` - Create service task
  - `task_kill()` - Stop service
  - `task_set_fn_null()` - Disable task after completion
  - `task_current_id()` - Get current task ID
- **ramfs.h/c** - Filesystem for unit files and logs
  - `ramfs_read()` - Load unit files
  - `ramfs_create()`, `ramfs_write()` - Output redirection
  - `ramfs_list()` - Enumerate service units
- **kmalloc.h/c** - Memory allocation for service structures
- **uart.h** - Console output for debug messages

### Syscall Integration

Service management is exposed via syscalls in `syscall.h`:

```c
#define SYS_SERVICE_LOAD_ALL    16
#define SYS_SERVICE_LOAD_UNIT   17
#define SYS_SERVICE_START       18
#define SYS_SERVICE_STOP        19
#define SYS_SERVICE_RESTART     20
#define SYS_SERVICE_RELOAD      21
#define SYS_SERVICE_ENABLE      22
#define SYS_SERVICE_DISABLE     23
#define SYS_SERVICE_STATUS      24
```

**Example (from userspace or programs):**
```c
syscall(SYS_SERVICE_START, "nginx", 0, 0);
```

### Programs Integration

The `systemctl` program (in `programs.c`) provides CLI for service management:

```bash
$ systemctl start nginx
$ systemctl stop nginx
$ systemctl status nginx
$ systemctl enable nginx
```

## Thread Safety

**Not Thread-Safe:** The service manager is designed for single-threaded access:
- Global linked list has no locking
- Service state updates are not atomic
- Concurrent service operations can corrupt data structures

**Workaround:** Access service manager from a single task (e.g., system manager task).

**Future:** Add mutex protection for concurrent access.

## Performance Considerations

### Memory Usage
- Per-service overhead: ~100 bytes (struct + path strings)
- Typical system: 10 services × 100 bytes = 1KB
- Service task: ~2KB stack + program memory

### Lookup Performance
- Linear search: O(n) where n = number of services
- Typical: n < 20 → negligible overhead (<1μs)
- Alternative: Hash table would be O(1) but adds complexity

### Startup Time
- Load all services: ~1-5ms per service (ramfs read + parse)
- Start service: ~100μs (task creation) + program init time

### Optimization Opportunities
- Cache service list in memory (avoid repeated ramfs reads)
- Use hash table for large service counts
- Parallel service startup for faster boot

## Error Handling

### Service Not Found
```c
int ret = service_start("nosuchservice");
// ret == -1, error message logged
```

### Unit File Parse Error
```c
// No ExecStart= in file
int ret = service_load_unit("/etc/systemd/system/bad.service");
// ret == -1, service not added to registry
```

### Out of Memory
```c
int pid = service_start("bigservice");
// pid == -1, task creation failed
```

### Service Already Running
```c
service_start("nginx");
int ret = service_start("nginx");
// ret == 0 (success, already running)
```

### Program Not Found
```c
// ExecStart=nosuchprogram
service_start("badservice");
// Service starts, logs error, then stops
// Check logs: "program lookup failed for: nosuchprogram"
```

## Future Enhancements

### Planned Features
1. **Auto-start:** Start enabled services at boot
2. **Dependencies:** Before=, After=, Requires=, Wants=
3. **Restart policies:** Restart=always, on-failure, etc.
4. **Resource limits:** Memory, CPU, file descriptor limits
5. **Service groups:** Target units (multi-user.target, etc.)
6. **Enhanced status:** Last start time, failure count
7. **Logging:** Structured logging with timestamps
8. **Signals:** Support SIGHUP for graceful reload

### API Extensions
```c
// Planned functions
int service_send_signal(const char *name, int signal);
int service_list_all(struct service_info *buf, int max);
int service_get_logs(const char *name, char *buf, size_t len);
```

### Configuration Extensions
```ini
[Service]
ExecStart=/path/to/program
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
User=www-data
WorkingDirectory=/var/www
```
