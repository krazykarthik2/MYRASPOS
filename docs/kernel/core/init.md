# init.c/h - Init Task (PID 1)

## Overview

`init.c` and `init.h` implement the init task, the first user-level task (PID 1) in the MYRASPOS system. The init task is responsible for completing system initialization that requires task context, starting essential services, initializing the GUI subsystem, and launching the interactive shell.

**Purpose:**
- Complete system initialization in task context
- Create default service units
- Load and start system services
- Initialize GUI and input subsystems
- Synchronize filesystems (diskfs ↔ ramfs)
- Launch the shell task
- Provide syscall wrapper functions for shell and other tasks

**Key Responsibilities:**
1. Service management setup (systemd-like units)
2. VirtIO input device initialization
3. VFS (Virtual File System) initialization
4. DiskFS synchronization to ramfs
5. Window manager initialization and startup
6. Shell task creation
7. Filesystem persistence (ramfs → diskfs sync)

## Boot Flow

### Init Task Lifecycle

```
kernel_main()
  │
  ├─> task_create_with_stack(init_main, NULL, "init", 64)
  │    └─ Init task created with 64 KB stack
  │
  ├─> schedule() called
  │
  └─> First context switch
       │
       └─> swtch.S:cpu_switch_to() → ret_from_fork
            │
            └─> init_main(NULL) executes
                 │
                 ├─ [1] Create service units
                 ├─ [2] Load and start services
                 ├─ [3] Initialize virtio-input
                 ├─ [4] Initialize VFS
                 ├─ [5] Sync diskfs → ramfs
                 ├─ [6] Initialize window manager
                 ├─ [7] Start shell task
                 ├─ [8] Sync ramfs → diskfs
                 │
                 └─> while(1) { yield(); }  // Idle forever
```

### Initialization Sequence

#### Phase 1: Service System Setup
```c
init_puts("[init] starting services...\n");

// Create filesystem directories
init_ramfs_mkdir("/etc/");
init_ramfs_mkdir("/var/");
init_ramfs_mkdir("/etc/systemd/system/");
```

Creates the directory structure:
```
/
├── etc/
│   └── systemd/
│       └── system/
│           ├── info.service
│           └── boot.service
└── var/
```

#### Phase 2: Service Unit Creation

**info.service** - System information logger:
```ini
[Unit]
Description=System Information Service

[Service]
ExecStart=help > /var/log/system.info
```

**boot.service** - Boot completion marker:
```ini
[Unit]
Description=Boot Logger

[Service]
ExecStart=echo Service System Started > /var/log/boot.log
```

These services demonstrate the service management system and log system status.

#### Phase 3: Service Loading and Starting
```c
init_service_load_all();      // Parse all .service files
init_service_start("boot");   // Start boot marker
init_service_start("info");   // Start info logger
```

#### Phase 4: Input and Filesystem Initialization
```c
virtio_input_init();          // Initialize keyboard/mouse
files_init();                 // Initialize VFS layer
diskfs_init();                // Initialize disk filesystem
diskfs_sync_to_ramfs();       // Load persistent files to RAM
```

#### Phase 5: GUI Subsystem
```c
init_puts("[init] GUI subsystem starting...\n");
wm_init();                    // Initialize window manager
wm_start_task();              // Start WM task
```

The window manager runs as a separate task to handle:
- Window compositing
- Input event routing
- Application window management

#### Phase 6: Shell Launch
```c
init_puts("[init] starting shell...\n");
init_start_shell();           // Create shell task
```

The shell task provides interactive command execution.

#### Phase 7: Filesystem Persistence
```c
diskfs_sync_from_ramfs();     // Save RAM files to disk
```

Ensures files created during init (service units, logs) are persisted.

#### Phase 8: Idle Loop
```c
for (;;) {
    yield();
}
```

Init never exits. It yields CPU to other tasks indefinitely.

## Data Structures

### Init Task Control
```c
static int initialized = 0;  // One-time initialization guard
```

Prevents re-initialization if init_main is somehow called multiple times.

## Key Functions

### Syscall Wrapper Functions

These functions provide a clean API for shell and other tasks to perform operations via syscalls:

#### `void init_puts(const char *s)`

**Description:** Print a string to console.

**Parameters:**
- `s` - Null-terminated string to print

**Implementation:**
```c
void init_puts(const char *s) {
    syscall_handle(SYS_PUTS, (uintptr_t)s, 0, 0);
}
```

**Syscall:** `SYS_PUTS` (syscall #1)

#### `char init_getc(void)`

**Description:** Read a single character from console (blocking).

**Returns:** Character read from input

**Implementation:**
```c
char init_getc(void) {
    return (char)syscall_handle(SYS_GETC, 0, 0, 0);
}
```

**Syscall:** `SYS_GETC` (syscall #3)

### Ramfs Operations

#### `int init_ramfs_create(const char *name)`

**Description:** Create a new file in ramfs.

**Parameters:**
- `name` - Path to file (e.g., "/etc/config.txt")

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_CREATE`

#### `int init_ramfs_write(const char *name, const void *buf, size_t len, int append)`

**Description:** Write data to a ramfs file.

**Parameters:**
- `name` - File path
- `buf` - Data to write
- `len` - Number of bytes
- `append` - If non-zero, append to existing content; otherwise overwrite

**Returns:** Number of bytes written, negative on error

**Implementation Details:**
- **Overwrite mode** (`append = 0`): Direct syscall
- **Append mode** (`append = 1`):
  1. Read existing content
  2. Allocate buffer for combined content
  3. Concatenate old + new
  4. Remove old file
  5. Create new file
  6. Write combined content
  7. Free temporary buffers

**Syscall:** `SYS_RAMFS_WRITE`

#### `int init_ramfs_read(const char *name, void *buf, size_t len)`

**Description:** Read data from a ramfs file.

**Parameters:**
- `name` - File path
- `buf` - Buffer to store data
- `len` - Maximum bytes to read

**Returns:** Number of bytes read, negative on error

**Syscall:** `SYS_RAMFS_READ`

#### `int init_ramfs_remove(const char *name)`

**Description:** Delete a file from ramfs.

**Parameters:**
- `name` - File path

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_REMOVE`

#### `int init_ramfs_mkdir(const char *name)`

**Description:** Create a directory in ramfs.

**Parameters:**
- `name` - Directory path (e.g., "/etc/config/")

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_MKDIR`

#### `int init_ramfs_list(const char *dir, char *buf, size_t len)`

**Description:** List files in a directory.

**Parameters:**
- `dir` - Directory path
- `buf` - Buffer to store listing (newline-separated)
- `len` - Buffer size

**Returns:** Number of bytes written, negative on error

**Syscall:** `SYS_RAMFS_LIST`

#### `int init_ramfs_remove_recursive(const char *path)`

**Description:** Recursively delete a directory and its contents.

**Parameters:**
- `path` - Directory path

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_REMOVE_RECURSIVE`

#### `int init_ramfs_export(const char *path)`

**Description:** Export a ramfs file to diskfs.

**Parameters:**
- `path` - File path

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_EXPORT`

#### `int init_ramfs_import(const char *path)`

**Description:** Import a file from diskfs to ramfs.

**Parameters:**
- `path` - File path

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_RAMFS_IMPORT`

### Service Manager Operations

#### `int init_service_load_all(void)`

**Description:** Load all service unit files from `/etc/systemd/system/`.

**Returns:** Number of services loaded, negative on error

**Syscall:** `SYS_SERVICE_LOAD_ALL`

#### `int init_service_load_unit(const char *path)`

**Description:** Load a specific service unit file.

**Parameters:**
- `path` - Path to .service file

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_LOAD_UNIT`

#### `int init_service_start(const char *name)`

**Description:** Start a service.

**Parameters:**
- `name` - Service name (without .service extension)

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_START`

#### `int init_service_stop(const char *name)`

**Description:** Stop a running service.

**Parameters:**
- `name` - Service name

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_STOP`

#### `int init_service_restart(const char *name)`

**Description:** Restart a service (stop then start).

**Parameters:**
- `name` - Service name

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_RESTART`

#### `int init_service_reload(const char *name)`

**Description:** Reload service configuration.

**Parameters:**
- `name` - Service name

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_RELOAD`

#### `int init_service_enable(const char *name)`

**Description:** Enable service to start on boot.

**Parameters:**
- `name` - Service name

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_ENABLE`

#### `int init_service_disable(const char *name)`

**Description:** Disable service from starting on boot.

**Parameters:**
- `name` - Service name

**Returns:** 0 on success, negative on error

**Syscall:** `SYS_SERVICE_DISABLE`

#### `int init_service_status(const char *name, char *buf, size_t len)`

**Description:** Get service status information.

**Parameters:**
- `name` - Service name
- `buf` - Buffer to store status string
- `len` - Buffer size

**Returns:** Number of bytes written, negative on error

**Syscall:** `SYS_SERVICE_STATUS`

### Shell Integration

#### `void init_start_shell(void)`

**Description:** Create and start the shell task.

**Implementation:**
```c
extern void shell_main(void *arg);

void init_start_shell(void) {
    task_create(shell_main, NULL, "shell");
}
```

Creates a new task running `shell_main()` from `shell.c`.

### Main Entry Point

#### `void init_main(void *arg)`

**Description:** Init task entry point (PID 1).

**Parameters:**
- `arg` - Argument passed from task creation (unused, always NULL)

**Flow:**
```c
void init_main(void *arg) {
    (void)arg;
    
    // Prevent re-initialization
    static int initialized = 0;
    if (initialized) {
        for (;;) yield();
    }
    initialized = 1;
    
    // [1] Create service system
    init_ramfs_mkdir("/etc/");
    init_ramfs_mkdir("/var/");
    init_ramfs_mkdir("/etc/systemd/system/");
    
    // [2] Create service units
    // ... info.service, boot.service ...
    
    // [3] Start services
    init_service_load_all();
    init_service_start("boot");
    init_service_start("info");
    
    // [4] Initialize input
    virtio_input_init();
    
    // [5] Initialize VFS and diskfs
    files_init();
    diskfs_init();
    diskfs_sync_to_ramfs();
    
    // [6] Start GUI
    wm_init();
    wm_start_task();
    
    // [7] Start shell
    init_start_shell();
    
    // [8] Persist files
    diskfs_sync_from_ramfs();
    
    // [9] Idle forever
    for (;;) {
        yield();
    }
}
```

## Implementation Details

### Syscall Abstraction Layer
All init functions use `syscall_handle()` to invoke kernel services:

```c
syscall_handle(syscall_number, arg0, arg1, arg2)
```

This provides:
- **Clean API** - Hides syscall details from callers
- **Type safety** - Function signatures match expected usage
- **Consistency** - All kernel services accessed uniformly

### Append Mode Implementation
The append functionality in `init_ramfs_write()` is implemented at the init level, not in the kernel:

**Why?**
- Kernel ramfs is simple and fast (no seeking, no append mode)
- Append is a convenience feature for user code
- Keeps kernel code minimal

**Tradeoffs:**
- Requires extra memory allocations
- Temporary buffer copies
- File recreation overhead

For append-heavy workloads, consider using a dedicated log service.

### One-Time Initialization Guard
```c
static int initialized = 0;
if (initialized) {
    for (;;) yield();
}
initialized = 1;
```

**Purpose:**
- Prevent multiple initializations if init_main is re-entered
- Graceful handling of unexpected re-scheduling

**Behavior:**
- First call: Performs full initialization
- Subsequent calls: Yields forever (no-op)

### Large Stack Requirement
Init task is created with 64 KB stack:
```c
task_create_with_stack(init_main, NULL, "init", 64);
```

**Reasoning:**
- VirtIO initialization has deep call stacks
- Service parsing and processing needs buffer space
- DiskFS operations may be stack-intensive
- GUI initialization allocates on stack

### Service Unit Format
MYRASPOS uses systemd-inspired .service files:

```ini
[Unit]
Description=Human-readable description

[Service]
ExecStart=command to execute
```

Stored in `/etc/systemd/system/` directory.

### Filesystem Synchronization
```
┌──────────┐         ┌──────────┐
│  ramfs   │ ←sync─  │  diskfs  │
│  (RAM)   │         │  (Disk)  │
└──────────┘         └──────────┘
     │
     │ sync→
     ↓
┌──────────┐
│  diskfs  │
│  (Disk)  │
└──────────┘
```

1. **Boot:** `diskfs_sync_to_ramfs()` - Load persistent files
2. **Runtime:** All operations on ramfs (fast)
3. **Shutdown:** `diskfs_sync_from_ramfs()` - Persist changes

## Design Decisions

### Why is Init a Task?
Many initialization operations need task context:
- Blocking I/O operations
- Service task creation
- Filesystem mounting
- Device probing with timeouts

Running init as a task allows these operations naturally.

### Why Not Start Services from Kernel?
**Separation of concerns:**
- Kernel: Low-level mechanisms (scheduler, memory, drivers)
- Init: Policy and service management

This design allows service configuration to be modified without kernel changes.

### Why Create Default Services in Code?
**Bootstrap problem:**
- Service system needs files in ramfs
- Files need service system to be useful
- Init creates minimal defaults to bootstrap

In the future, these could be loaded from a read-only root filesystem.

### Why Sync Ramfs → Diskfs at End?
**Persistence:**
- Service units created during init
- Log files generated by services
- Configuration files

Must be saved to disk to persist across reboots.

### Why Does Init Idle Forever?
**Traditional Unix behavior:**
- Init (PID 1) never exits
- Serves as parent for orphaned processes (future)
- Allows system to remain stable

If init exits, the system has no root task, causing instability.

## Usage Examples

### Creating a Custom Service from Shell

**Step 1: Write service unit**
```bash
echo "[Unit]
Description=My Custom Service

[Service]
ExecStart=my_command arg1 arg2" > /etc/systemd/system/custom.service
```

**Step 2: Load the service**
```bash
systemctl load /etc/systemd/system/custom.service
```

**Step 3: Start the service**
```bash
systemctl start custom
```

### Adding Init Functionality

**Example: Auto-mount USB device**

```c
void init_main(void *arg) {
    /* ... existing initialization ... */
    
    // Add USB mount logic
    init_puts("[init] checking for USB devices...\n");
    if (usb_device_present()) {
        init_ramfs_mkdir("/mnt/usb/");
        usb_mount("/mnt/usb/");
        init_puts("[init] USB device mounted at /mnt/usb/\n");
    }
    
    /* ... continue with existing code ... */
}
```

### Using Init Functions from Shell

The shell uses init functions for all filesystem operations:

```c
// In shell.c
void shell_cmd_ls(const char *path) {
    char buf[4096];
    int n = init_ramfs_list(path, buf, sizeof(buf));
    if (n > 0) {
        init_puts(buf);
    } else {
        init_puts("Error listing directory\n");
    }
}
```

## Cross-References

### Related Documentation
- [kernel.md](kernel.md) - Kernel initialization and init task creation
- [swtch.md](swtch.md) - Context switch to init task (ret_from_fork)
- [../sched/scheduler.md](../sched/scheduler.md) - Task creation and scheduling
- [../syscall/syscall.md](../syscall/syscall.md) - Syscall mechanism
- [../fs/ramfs.md](../fs/ramfs.md) - In-memory filesystem
- [../fs/diskfs.md](../fs/diskfs.md) - Persistent filesystem
- [../services/service.md](../services/service.md) - Service manager
- [../gui/wm.md](../gui/wm.md) - Window manager
- [../shell/shell.md](../shell/shell.md) - Interactive shell

### Call Chain: Init Task Creation

```
kernel_main()
  │
  └─> task_create_with_stack(init_main, NULL, "init", 64)
       │
       └─> sched.c:task_create_with_stack()
            │
            ├─ Allocate TCB (Task Control Block)
            ├─ Allocate 64 KB stack
            │
            ├─ Initialize task_context:
            │   ├─ x19 = init_main
            │   ├─ x20 = NULL (arg)
            │   ├─ x30 = ret_from_fork
            │   └─ sp = stack_top
            │
            └─> Add to ready queue
                 │
                 └─> schedule() picks init
                      │
                      └─> cpu_switch_to(NULL, &init_context)
                           │
                           └─> swtch.S loads context
                                │
                                └─> ret → ret_from_fork
                                     │
                                     └─> blr x19 → init_main(x20)
```

### Syscall Invocation Chain

```
Shell/Init Task
  │
  └─> init_ramfs_create("/etc/config")
       │
       └─> syscall_handle(SYS_RAMFS_CREATE, "/etc/config", 0, 0)
            │
            └─> asm: svc #0  [Supervisor Call instruction]
                 │
                 └─> vectors.S:el0_sync handler
                      │
                      └─> exception_c_handler()
                           │
                           └─> syscall_handle() [in kernel]
                                │
                                └─> ramfs_create()
                                     │
                                     └─> Return to user task
```

### Service System Flow

```
init_main()
  │
  ├─> Create .service files in ramfs
  │    └─> init_ramfs_create() / init_ramfs_write()
  │
  ├─> init_service_load_all()
  │    │
  │    └─> SYS_SERVICE_LOAD_ALL syscall
  │         │
  │         └─> service.c:services_load_all()
  │              │
  │              ├─ Scan /etc/systemd/system/
  │              ├─ Parse .service files
  │              └─ Build service registry
  │
  └─> init_service_start("boot")
       │
       └─> SYS_SERVICE_START syscall
            │
            └─> service.c:service_start()
                 │
                 ├─ Look up "boot" in registry
                 ├─ Parse ExecStart command
                 ├─ Create task for service
                 └─ Add to active services list
```

### Filesystem Sync Flow

```
                  ┌─────────────┐
                  │   diskfs    │
                  │   (Disk)    │
                  └─────────────┘
                        │
                        │ diskfs_sync_to_ramfs()
                        │ (Load at boot)
                        ↓
                  ┌─────────────┐
                  │   ramfs     │ ← All runtime operations
                  │   (RAM)     │
                  └─────────────┘
                        │
                        │ diskfs_sync_from_ramfs()
                        │ (Save before shutdown)
                        ↓
                  ┌─────────────┐
                  │   diskfs    │
                  │   (Disk)    │
                  └─────────────┘
```

### Task Hierarchy

```
kernel_main() [kernel context]
  │
  └─> init [PID 1] (init_main)
       │
       ├─> boot.service [task]
       ├─> info.service [task]
       ├─> wm [task] (window manager)
       │    │
       │    └─> terminal_app [task]
       │
       └─> shell [task] (shell_main)
            │
            └─> programs spawned by shell
```

Init serves as the ancestor of all system tasks, though not all are direct children in current implementation.
