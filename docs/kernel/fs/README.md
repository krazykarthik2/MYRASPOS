# MYRASPOS Filesystem Subsystem Documentation

## Overview

This directory contains comprehensive technical documentation for the MYRASPOS filesystem subsystems. The filesystem architecture consists of three layers working together to provide fast, transparent, and persistent file storage.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Applications & Shell                      │
│              (cat, ls, cp, edit, programs, etc.)            │
└────────────────────────┬────────────────────────────────────┘
                         │
                         │ System Calls / init_* wrappers
                         │
┌────────────────────────▼────────────────────────────────────┐
│              files.c - File Abstraction Layer                │
│  • POSIX-like API (open, close, read, write, seek, stat)   │
│  • File descriptor management (32 FDs)                      │
│  • Lazy loading from disk to RAM                           │
│  • Write-through caching for persistence                   │
└──────────┬─────────────────────────────┬───────────────────┘
           │                             │
           │ RAMFS API                   │ DiskFS API
           │ (fast access)               │ (persistence)
           │                             │
┌──────────▼──────────────┐    ┌─────────▼──────────────────┐
│   ramfs.c                │    │   diskfs.c                 │
│   RAM Filesystem         │◄───┤   Disk Filesystem          │
│   • In-memory storage    │sync│   • VirtIO block backend  │
│   • Linked list          │    │   • Sector-based I/O       │
│   • Path cache (32)      │    │   • Directory table (128)  │
│   • Max: system RAM      │    │   • Sequential allocation  │
└──────────────────────────┘    └─────────┬──────────────────┘
                                          │
                                          │ VirtIO API
                                          │
                                ┌─────────▼──────────────────┐
                                │   virtio.c                 │
                                │   VirtIO Block Driver      │
                                │   • Paravirtualized I/O    │
                                │   • 512-byte sectors       │
                                └────────────────────────────┘
```

## Core Components

### 1. [ramfs.c](ramfs.c.md) - RAM Filesystem
**Purpose:** Fast, volatile, in-memory filesystem  
**Type:** Linked-list based flat namespace  
**Performance:** O(1) avg with cache, O(n) worst case  

**Key Features:**
- All file data stored in kernel heap (kmalloc)
- 32-entry LRU path cache for fast lookups
- Full path strings as node names (e.g., `/home/user/file.txt`)
- Directories identified by trailing `/` in name
- Import/export for serialization
- No persistence - contents lost on reboot

**When to Use:**
- Working storage for active files
- Fast temporary files
- Frequently accessed data
- Shell command I/O buffers

**API Highlights:**
```c
int ramfs_create(const char *name);
int ramfs_write(const char *name, const void *buf, size_t len, size_t offset);
int ramfs_read(const char *name, void *buf, size_t len, size_t offset);
int ramfs_remove(const char *name);
int ramfs_mkdir(const char *name);
int ramfs_list(const char *dir, char *buf, size_t len);
```

---

### 2. [diskfs.c](diskfs.c.md) - Disk Filesystem
**Purpose:** Persistent storage via VirtIO block device  
**Type:** Directory-table filesystem with fixed metadata  
**Performance:** Disk I/O bound (10-1000μs per operation)  

**Key Features:**
- Up to 128 files in fixed directory table
- 512-byte sector-based I/O
- Sequential sector allocation (no fragmentation)
- Synchronizes with RAMFS for boot loading and shutdown syncing
- Directory table cached in RAM
- No file deletion (no space reclamation)

**When to Use:**
- Files that must survive reboot
- System configuration files
- User data persistence
- Backup/recovery

**API Highlights:**
```c
void diskfs_init(void);
int diskfs_create(const char *name);
int diskfs_write(const char *name, const void *buf, size_t len, size_t offset);
int diskfs_read(const char *name, void *buf, size_t len, size_t offset);
void diskfs_sync_from_ramfs(void);  // RAM → Disk
void diskfs_sync_to_ramfs(void);    // Disk → RAM
```

---

### 3. [files.c](files.c.md) - File Abstraction Layer
**Purpose:** POSIX-like file descriptor interface  
**Type:** Unified abstraction over RAMFS and DiskFS  
**Performance:** Fast reads (RAMFS), slow writes (write-through to disk)  

**Key Features:**
- File descriptor table (32 open files max)
- Lazy loading: disk files loaded to RAM on first access
- Write-through caching: writes go to both RAM and disk
- Per-descriptor position tracking (seek support)
- POSIX-compatible flags (O_RDONLY, O_CREAT, O_APPEND, etc.)
- Automatic file creation and truncation

**When to Use:**
- Standard application file I/O
- Sequential read/write operations
- Shell command implementations
- Any code needing portable file access

**API Highlights:**
```c
int files_open(const char *path, int flags);
int files_close(int fd);
int files_read(int fd, void *buf, size_t len);
int files_write(int fd, const void *buf, size_t len);
int files_seek(int fd, int offset, int whence);
int files_stat(const char *path, struct file_stat *st);
```

---

## Data Flow Examples

### Reading a File

```
1. Application: fd = files_open("/config.txt", O_RDONLY)
   ↓
2. files.c: Check if file in RAMFS
   ↓
3. files.c: Not found → load_from_disk_if_needed()
   ↓
4. diskfs.c: diskfs_read("/config.txt", buffer, 65536, 0)
   ↓
5. virtio.c: Read sectors from disk
   ↓
6. files.c: ramfs_create() + ramfs_write() (cache in RAM)
   ↓
7. files.c: Allocate FD, return to application
   
Application: files_read(fd, buf, 100)
   ↓
8. files.c: ramfs_read() directly (fast!)
   ↓
9. Return data to application
```

**Performance:** First open: slow (disk I/O). Subsequent reads: fast (RAM).

---

### Writing a File

```
Application: fd = files_open("/log.txt", O_WRONLY | O_CREAT)
   ↓
1. files.c: Create empty file in RAMFS if needed
   ↓
Application: files_write(fd, "data", 4)
   ↓
2. files.c: ramfs_write("/log.txt", "data", 4, pos)
   ↓
3. files.c: diskfs_create("/log.txt")
   ↓
4. files.c: diskfs_write("/log.txt", "data", 4, pos)
   ↓
5. diskfs.c: Write to sectors via virtio_blk_rw()
   ↓
6. diskfs.c: Update directory table, save_dir()
   ↓
7. Return to application
```

**Performance:** Every write blocks on disk I/O (write-through).

---

### Boot Sequence

```
Kernel Initialization:
1. ramfs_init()            // Initialize empty RAM filesystem
2. diskfs_init()           // Load disk directory table
3. diskfs_sync_to_ramfs()  // Populate RAM with disk files
4. files_init()            // Initialize FD table

Result: All persistent files now in RAM, ready for fast access
```

---

### Shutdown Sequence

```
Before Shutdown:
1. diskfs_sync_from_ramfs()  // Persist any new RAM files to disk

Result: All files saved to disk, safe to power off
```

---

## File Lifecycle

### Creating and Writing a New File

```c
// Via files.c API (recommended)
int fd = files_open("/newfile.txt", O_WRONLY | O_CREAT);
files_write(fd, "Hello", 5);
files_close(fd);
// Result: File in RAMFS and persisted to DiskFS

// Via RAMFS directly (no persistence)
ramfs_create("/tempfile.txt");
ramfs_write("/tempfile.txt", "Temp", 4, 0);
// Result: File only in RAMFS, lost on reboot
```

---

### Reading an Existing Persistent File

```c
// At boot: diskfs_sync_to_ramfs() loaded all disk files to RAM

int fd = files_open("/system/config.txt", O_RDONLY);
char buf[256];
files_read(fd, buf, sizeof(buf));
files_close(fd);

// No disk I/O after boot - reads from RAMFS cache
```

---

### Updating a Persistent File

```c
int fd = files_open("/config.txt", O_RDWR);
files_seek(fd, 10, SEEK_SET);  // Seek to offset 10
files_write(fd, "new", 3);     // Overwrites 3 bytes
files_close(fd);

// Write went to both RAMFS (fast) and DiskFS (persistent)
```

---

## Performance Characteristics

| Operation | RAMFS | DiskFS | files.c |
|-----------|-------|--------|---------|
| Read (cached) | ~1 μs | N/A | ~1 μs |
| Read (uncached) | N/A | 10-1000 μs | 10-1000 μs (first access) |
| Write | ~1 μs | 10-1000 μs | 10-1000 μs (write-through) |
| Open (cached) | N/A | N/A | ~1 μs |
| Open (uncached) | N/A | 10-1000 μs | 10-1000 μs |
| Seek | N/A | N/A | ~0.1 μs |
| Stat | ~1 μs | N/A | ~1 μs (cached) |
| List directory | O(n) | N/A | O(n) |

**Bottlenecks:**
- DiskFS writes block on VirtIO (hypervisor context switch)
- RAMFS list() is O(n²) with duplicate checking
- files.c write-through doubles latency

---

## Limitations and Trade-offs

### RAMFS Limitations
- **No persistence** - contents lost on reboot (use export/import)
- **O(n) lookups** - linked list with 32-entry cache only
- **Flat namespace** - directories simulated via path prefixes
- **No quotas** - can exhaust kernel heap
- **Not thread-safe** - requires external locking

### DiskFS Limitations
- **128 file maximum** - fixed directory table size
- **No deletion** - space never reclaimed
- **No subdirectories** - flat namespace only
- **Sequential allocation** - can't handle fragmentation
- **64KB file size limit** (for lazy loading in files.c)
- **Write-through only** - slow write performance

### files.c Limitations
- **32 file descriptors max** - system-wide limit
- **Global FD table** - no per-process isolation
- **64KB lazy load limit** - larger files can't be loaded from disk
- **No error codes** - simple -1/0/n return values
- **No advanced features** - no mmap, dup, fcntl, etc.

---

## Design Decisions

### Why Separate RAMFS and DiskFS?

**Alternative:** Single unified filesystem with caching.

**Choice:** Separate layers with explicit synchronization.

**Rationale:**
- **Simplicity:** Each layer has clear responsibility
- **Flexibility:** Can use RAMFS without disk, or disk without caching
- **Debugging:** Easy to trace data flow between layers
- **Performance:** Explicit control over when disk I/O occurs

**Trade-off:** Manual synchronization required (sync_to/from_ramfs).

---

### Why Linked List for RAMFS?

**Alternatives:** Hash table, B-tree, trie.

**Choice:** Simple singly-linked list.

**Rationale:**
- **Embedded context:** Typically < 100 files
- **No dependencies:** No complex data structure libraries needed
- **Memory efficiency:** No overhead beyond node pointers
- **Cache compensates:** 32-entry cache provides O(1) for common cases

**Trade-off:** O(n) worst case, but n is small.

---

### Why Write-Through Caching?

**Alternative:** Write-back caching (buffer writes, periodic flush).

**Choice:** Write-through (every write hits disk).

**Rationale:**
- **Durability:** No data loss on crash or power failure
- **Simplicity:** No flush logic or dirty tracking
- **Correctness:** Always consistent state

**Trade-off:** Slower writes, but acceptable for embedded use case.

---

### Why 64KB Lazy Load Limit?

**Alternative:** Stream large files in chunks, or dynamic buffer sizing.

**Choice:** Fixed 64KB buffer in `load_from_disk_if_needed()`.

**Rationale:**
- **Embedded target:** Files typically small (configs, scripts, logs)
- **Simple implementation:** Single allocation, single read
- **Memory limit:** Avoid exhausting kernel heap

**Trade-off:** Can't lazy-load files > 64KB from disk.

---

## Usage Guidelines

### For Application Developers

**Use files.c API for all file I/O:**
```c
#include "files.h"

// Standard pattern
int fd = files_open("/myfile.txt", O_RDWR | O_CREAT);
if (fd < 0) {
    // Handle error
    return -1;
}

char data[256];
int n = files_read(fd, data, sizeof(data));
// Process data...

files_write(fd, "result", 6);
files_close(fd);
```

**Don't use RAMFS or DiskFS directly** unless you have specific needs (e.g., temporary non-persistent files).

---

### For Kernel Developers

**When to use RAMFS directly:**
- Temporary files that should NOT persist (swap space, temp buffers)
- Performance-critical paths where disk I/O unacceptable
- Internal kernel data structures stored as files

**When to use DiskFS directly:**
- Implementing custom persistence strategies
- Bulk loading at boot (diskfs_sync_to_ramfs)
- Implementing backup/snapshot features

**When to use files.c:**
- All other cases (default choice for file I/O)

---

### For Shell Command Developers

**Use init.c wrappers:**
```c
#include "init.h"

// Commands use init_ramfs_* wrappers for safety
int r = init_ramfs_read("/file.txt", buffer, sizeof(buffer));
init_ramfs_create("/newfile.txt");
init_ramfs_write("/newfile.txt", data, len, 0);
```

**Wrappers provide:**
- Path resolution (relative → absolute via shell CWD)
- Additional safety checks
- Consistent error handling

---

## Common Patterns

### Reading Configuration at Boot

```c
// In service initialization
diskfs_init();
diskfs_sync_to_ramfs();  // Load /system/config.txt from disk

int fd = files_open("/system/config.txt", O_RDONLY);
char config[1024];
int n = files_read(fd, config, sizeof(config));
parse_config(config, n);
files_close(fd);
```

---

### Appending to Log File

```c
// Open once, keep open for efficient logging
int log_fd = files_open("/var/log/system.log", O_WRONLY | O_CREAT | O_APPEND);

void log(const char *msg) {
    files_write(log_fd, msg, strlen(msg));
    // Auto-persisted to disk (write-through)
}

// At shutdown:
files_close(log_fd);
```

---

### Creating Directory Hierarchy

```c
// Create nested directories
ramfs_mkdir("/home");
ramfs_mkdir("/home/user");
ramfs_mkdir("/home/user/documents");

// Create file in deepest directory
ramfs_create("/home/user/documents/file.txt");
ramfs_write("/home/user/documents/file.txt", "data", 4, 0);

// Persist entire hierarchy to disk
diskfs_sync_from_ramfs();
```

---

### Backup and Restore

```c
// Export entire RAMFS to a single file
ramfs_export("/backup.ramfs");

// Write backup to disk
diskfs_sync_from_ramfs();  // Saves /backup.ramfs to disk

// Later, after reboot:
diskfs_sync_to_ramfs();     // Loads /backup.ramfs from disk
ramfs_import("/backup.ramfs");  // Restores all files
```

---

## Testing and Debugging

### Verify File Persistence

```bash
# Create file
echo "test data" > /persistent.txt

# Check in RAMFS
ls /

# Verify in DiskFS
# (at kernel level, check dir_cache)

# Reboot system
reboot

# After boot, verify file exists
cat /persistent.txt  # Should show "test data"
```

---

### Monitor Filesystem State

```c
// In kernel debug code
void dump_ramfs_state(void) {
    uart_puts("=== RAMFS State ===\n");
    for (struct ram_node *n = root; n; n = n->next) {
        uart_puts("File: "); uart_puts(n->name);
        uart_puts(" Size: "); uart_put_hex(n->size);
        uart_puts("\n");
    }
}

void dump_diskfs_state(void) {
    uart_puts("=== DiskFS State ===\n");
    uart_puts("Files: "); uart_put_hex(num_files); uart_puts("\n");
    uart_puts("Next sector: "); uart_put_hex(next_free_sector); uart_puts("\n");
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (dir_cache[i].name[0] != '\0') {
            uart_puts("  "); uart_puts(dir_cache[i].name);
            uart_puts(" -> sector "); uart_put_hex(dir_cache[i].start_sector);
            uart_puts(" size "); uart_put_hex(dir_cache[i].size);
            uart_puts("\n");
        }
    }
}
```

---

## Future Enhancements

### Short-Term Improvements

1. **DiskFS deletion support**
   - Implement `diskfs_remove()`
   - Free space bitmap
   - Directory entry recycling

2. **Larger lazy load buffer**
   - Increase from 64KB to 1MB
   - Or implement chunked streaming

3. **Better error reporting**
   - errno-style error codes
   - Detailed error messages

4. **Thread safety**
   - Mutexes around critical sections
   - Per-descriptor locks

---

### Long-Term Enhancements

1. **B-tree or hash table for RAMFS**
   - O(log n) or O(1) lookups
   - Handle thousands of files efficiently

2. **Write-back caching option**
   - Configurable write-through vs write-back
   - Periodic sync daemon
   - Dirty tracking

3. **Per-process file descriptors**
   - Isolated FD tables per process
   - Fork inheritance
   - Process limits

4. **Advanced features**
   - Memory mapping (mmap)
   - File locking
   - Async I/O
   - Vectored I/O

5. **Better disk filesystem**
   - Extent-based allocation
   - Directory tree structure
   - Journaling
   - Compression

---

## Related Documentation

- **[00-ARCHITECTURE.md](../../00-ARCHITECTURE.md)** - Overall kernel architecture
- **[virtio.c.md](../drivers/virtio.c.md)** - VirtIO block device driver
- **[init.c.md](../core/init.c.md)** - Kernel initialization
- **[syscall.c.md](../core/syscall.c.md)** - System call interface
- **[shell.c.md](../core/shell.c.md)** - Shell and command execution

---

## Source Code Locations

```
kernel/
├── ramfs.c          # RAM filesystem implementation
├── ramfs.h          # RAMFS public API
├── diskfs.c         # Disk filesystem implementation  
├── diskfs.h         # DiskFS public API
├── files.c          # File abstraction layer
├── files.h          # Files public API
├── virtio.c         # VirtIO block driver (used by diskfs)
├── init.c           # Wrappers (init_ramfs_*, init_diskfs_*)
└── commands/        # Shell commands using filesystem
    ├── cat.c
    ├── ls.c
    ├── cp.c
    ├── rm.c
    ├── mkdir.c
    └── ramfs_tools.c  # Import/export commands
```

---

**Last Updated:** 2024  
**Maintainer:** MYRASPOS Kernel Team  
**Status:** Production - stable interfaces, active development
