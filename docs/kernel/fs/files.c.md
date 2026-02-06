# files.c - File Abstraction Layer

## Overview

The Files subsystem provides a POSIX-like file descriptor abstraction layer for MYRASPOS. It implements standard file operations (open, close, read, write, seek) on top of the RAMFS and DiskFS backends, providing a unified interface for applications and kernel components.

### Architecture

Files.c implements a **file descriptor table** with lazy loading from DiskFS and write-through persistence. The architecture provides:

- **Unified namespace**: Applications access both RAM and disk files transparently
- **Lazy loading**: Files automatically loaded from disk to RAM on first access
- **Write-through caching**: Writes propagate to both RAMFS and DiskFS immediately
- **File positioning**: Per-descriptor cursor tracking for sequential I/O
- **POSIX-compatible API**: Similar to standard UNIX file operations

**Storage Model:**
```
Application
    ↓
files_open/read/write/seek
    ↓
File Descriptor Table (32 entries)
    ↓
RAMFS (fast, volatile) ←→ DiskFS (persistent, slow)
    ↓
VirtIO Block Device
```

### Integration Points

- **ramfs.c**: Primary storage backend for all file data
- **diskfs.c**: Persistent storage for durability across reboots
- **System Calls**: File operations exposed via syscall interface
- **Applications**: Programs use standard file I/O without knowing backend details

## Data Structures

### `struct file_desc`

Represents an open file descriptor with position tracking and flags.

```c
struct file_desc {
    int used;           // 1 if descriptor in use, 0 if free slot
    char path[128];     // Absolute path of open file
    size_t pos;         // Current read/write position (byte offset)
    int flags;          // Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
};
```

**Fields:**
- `used`: Boolean indicating if this FD slot is allocated
- `path`: Full absolute path, copied at open time
- `pos`: Byte offset for next read/write operation
- `flags`: Combination of open mode flags

### File Descriptor Table

```c
#define MAX_FDS 32
static struct file_desc fds[MAX_FDS];
```

**Characteristics:**
- Fixed-size array of 32 descriptors
- Indexed by file descriptor number (0-31)
- Global to kernel (not per-process)
- Simple linear search for allocation

### Open Flags

Standard POSIX-style flags for file operations:

```c
#define O_RDONLY 0      // Read-only mode
#define O_WRONLY 1      // Write-only mode
#define O_RDWR   2      // Read-write mode
#define O_CREAT  4      // Create if doesn't exist
#define O_TRUNC  8      // Truncate to zero length
#define O_APPEND 16     // Append mode (start at EOF)
```

**Flag Combinations:**
- `O_RDONLY`: Read operations only
- `O_WRONLY | O_CREAT`: Create and write new file
- `O_RDWR | O_TRUNC`: Open existing file, clear contents
- `O_WRONLY | O_APPEND`: Append to end of file

### Seek Whence Values

Position reference for `files_seek()`:

```c
#define SEEK_SET 0      // Absolute position
#define SEEK_CUR 1      // Relative to current position
#define SEEK_END 2      // Relative to end of file
```

### `struct file_stat`

File metadata returned by `files_stat()`:

```c
struct file_stat {
    size_t size;        // File size in bytes
    int is_dir;         // 1 if directory, 0 if regular file
};
```

## Key Functions

### Initialization

#### `void files_init(void)`

Initializes the file descriptor table by clearing all entries.

**Implementation:**
```c
void files_init(void) {
    memset(fds, 0, sizeof(fds));  // Zero all descriptors
}
```

**Called By:** Kernel initialization during boot (`init.c` or `main.c`).

**Thread Safety:** Must be called before any file operations.

---

### File Operations

#### `int files_open(const char *path, int flags)`

Opens a file and returns a file descriptor.

**Parameters:**
- `path`: Absolute path to file (e.g., `/home/user/file.txt`)
- `flags`: Open mode flags (O_RDONLY, O_WRONLY, O_CREAT, etc.)

**Returns:**
- File descriptor (0-31) on success
- `-1` on error (no free descriptors, file not found, creation failed)

**Implementation Details:**

1. **Find free FD slot:**
   - Linear search through `fds[]` for `used == 0`
   - Return -1 if all 32 slots occupied

2. **Lazy load from disk if needed:**
   - Check if file exists in RAMFS via `ramfs_get_size()`
   - If not in RAM, try loading from DiskFS:
     ```c
     diskfs_read(path, buffer, 65536, 0)
     ramfs_create(path)
     ramfs_write(path, buffer, bytes_read, 0)
     ```
   - Cache file in RAMFS for fast subsequent access

3. **Handle O_CREAT flag:**
   - If file doesn't exist and `O_CREAT` set, create empty file
   - If file doesn't exist and `O_CREAT` not set, fail with -1

4. **Handle O_TRUNC flag:**
   - If file exists and `O_TRUNC` set:
     ```c
     ramfs_remove(path);
     ramfs_create(path);  // Creates empty file
     ```

5. **Initialize descriptor:**
   - Set `used = 1`
   - Copy path into `fds[fd].path`
   - Set `pos = 0`
   - Save flags

6. **Handle O_APPEND flag:**
   - If `O_APPEND` set, seek to end:
     ```c
     int size = ramfs_get_size(path);
     fds[fd].pos = size;
     ```

**Lazy Loading:** First access to a disk file loads it entirely into RAMFS. Subsequent operations use fast RAMFS.

**Example:**
```c
// Open existing file for reading
int fd = files_open("/config.txt", O_RDONLY);

// Create new file for writing
int fd2 = files_open("/newfile.txt", O_WRONLY | O_CREAT);

// Open for append
int fd3 = files_open("/log.txt", O_WRONLY | O_APPEND | O_CREAT);

// Truncate existing file
int fd4 = files_open("/data.bin", O_RDWR | O_TRUNC);
```

---

#### `int files_close(int fd)`

Closes a file descriptor, freeing the slot for reuse.

**Parameters:**
- `fd`: File descriptor to close (0-31)

**Returns:**
- `0` on success
- `-1` if fd invalid or not open

**Implementation Details:**
1. Validate fd range (0-31)
2. Check if `fds[fd].used == 1`
3. Set `fds[fd].used = 0` to free slot
4. No flush needed (write-through already persisted data)

**No Flush Required:** Since writes are write-through to DiskFS, closing doesn't need to flush buffers.

**Example:**
```c
int fd = files_open("/file.txt", O_RDONLY);
// ... use file ...
files_close(fd);  // Free descriptor
```

---

#### `int files_read(int fd, void *buf, size_t len)`

Reads data from file at current position, advancing position.

**Parameters:**
- `fd`: File descriptor
- `buf`: Destination buffer
- `len`: Maximum bytes to read

**Returns:**
- Number of bytes actually read (may be less than `len`)
- `0` on EOF
- `-1` on error (invalid fd, file error)

**Implementation Details:**
1. Validate fd
2. Call `ramfs_read(path, buf, len, pos)`
3. If successful, advance position: `fds[fd].pos += bytes_read`
4. Return number of bytes read

**EOF Behavior:** Returns 0 when position at or beyond file size.

**Partial Reads:** May return fewer bytes than requested if EOF reached.

**Example:**
```c
char buffer[1024];
int fd = files_open("/data.txt", O_RDONLY);
int n = files_read(fd, buffer, sizeof(buffer));
if (n > 0) {
    buffer[n] = '\0';
    printf("Read: %s\n", buffer);
}
files_close(fd);
```

---

#### `int files_write(int fd, const void *buf, size_t len)`

Writes data to file at current position, advancing position.

**Parameters:**
- `fd`: File descriptor
- `buf`: Source data buffer
- `len`: Number of bytes to write

**Returns:**
- Number of bytes written on success
- `-1` on error

**Implementation Details:**

1. Validate fd
2. **Write to RAMFS:**
   ```c
   int w = ramfs_write(path, buf, len, pos);
   ```
3. **Write-through to DiskFS** (if RAMFS write successful):
   ```c
   diskfs_create(path);              // Ensure exists on disk
   diskfs_write(path, buf, len, pos); // Persist to disk
   ```
4. Advance position: `fds[fd].pos += w`
5. Return bytes written

**Write-Through Policy:**
- Every write propagates to both RAMFS and DiskFS
- Ensures durability at cost of performance
- No explicit flush needed

**File Growth:** Files automatically expand if writing beyond EOF.

**Atomicity:** Not atomic - partial writes possible if RAMFS succeeds but DiskFS fails.

**Example:**
```c
int fd = files_open("/log.txt", O_WRONLY | O_CREAT | O_APPEND);
char log[] = "Error: something happened\n";
files_write(fd, log, strlen(log));
files_close(fd);
```

---

#### `int files_seek(int fd, int offset, int whence)`

Changes the file position for subsequent read/write operations.

**Parameters:**
- `fd`: File descriptor
- `offset`: Position offset (interpretation depends on `whence`)
- `whence`: Reference point:
  - `SEEK_SET`: Absolute position (offset from start)
  - `SEEK_CUR`: Relative to current position
  - `SEEK_END`: Relative to end of file

**Returns:**
- New absolute position on success
- `-1` on error (invalid fd, file not found)

**Implementation Details:**

1. Validate fd
2. Get file size via `ramfs_get_size(path)`
3. Calculate new position based on whence:
   ```c
   if (whence == SEEK_SET)
       new_pos = offset;
   else if (whence == SEEK_CUR)
       new_pos = current_pos + offset;
   else if (whence == SEEK_END)
       new_pos = file_size + offset;
   ```
4. Clamp negative positions to 0: `if (new_pos < 0) new_pos = 0;`
5. Allow seeking past EOF (reads will return 0, writes will expand file)
6. Update `fds[fd].pos = new_pos`
7. Return new position

**Seeking Past EOF:** Allowed - subsequent read returns 0, write expands file.

**Negative Offsets:** With `SEEK_END`, can seek backward from end.

**Example:**
```c
int fd = files_open("/data.bin", O_RDWR);

// Seek to start
files_seek(fd, 0, SEEK_SET);

// Seek 100 bytes forward
files_seek(fd, 100, SEEK_CUR);

// Seek to 10 bytes before end
files_seek(fd, -10, SEEK_END);

// Get current position
int pos = files_seek(fd, 0, SEEK_CUR);

files_close(fd);
```

---

### Metadata Operations

#### `int files_stat(const char *path, struct file_stat *st)`

Retrieves file metadata (size, type).

**Parameters:**
- `path`: File path
- `st`: Pointer to `file_stat` structure to fill

**Returns:**
- `0` on success
- `-1` on error (file not found)

**Implementation Details:**

1. **Lazy load from disk if needed:**
   ```c
   load_from_disk_if_needed(path);
   ```
2. Get file size via `ramfs_get_size(path)`
3. Check if directory via `ramfs_is_dir(path)`
4. Fill `st->size` and `st->is_dir`
5. Return 0

**Lazy Loading:** Like `files_open()`, automatically loads from disk if not in RAMFS.

**Directory Detection:** Uses RAMFS directory identification (trailing `/` or has children).

**Example:**
```c
struct file_stat st;
if (files_stat("/config.txt", &st) == 0) {
    printf("Size: %zu bytes\n", st.size);
    printf("Type: %s\n", st.is_dir ? "directory" : "file");
}
```

---

### Internal Functions

#### `static int load_from_disk_if_needed(const char *path)`

Lazy-loads a file from DiskFS into RAMFS if not already present.

**Algorithm:**

1. Check if file exists in RAMFS:
   ```c
   if (ramfs_get_size(path) >= 0)
       return 0;  // Already in RAM
   ```

2. Attempt to read from DiskFS:
   ```c
   void *buffer = kmalloc(65536);
   int bytes = diskfs_read(path, buffer, 65536, 0);
   ```

3. If found in DiskFS:
   - Create file in RAMFS: `ramfs_create(path)`
   - Write data to RAMFS: `ramfs_write(path, buffer, bytes, 0)`
   - Free temporary buffer
   - Return 0

4. If not found:
   - Free buffer
   - Return -1

**Maximum File Size:** Limited to 64KB by hardcoded buffer allocation.

**Performance:** One-time cost on first access, then fast RAMFS operations.

**Example:**
```c
// Internal use - automatically called by files_open() and files_stat()
load_from_disk_if_needed("/system/config.txt");
// Now file is in RAMFS, subsequent operations are fast
```

---

## Implementation Details

### Lazy Loading Architecture

**Goal:** Keep frequently accessed files in fast RAMFS while supporting large persistent storage on disk.

**Mechanism:**
1. First access triggers `load_from_disk_if_needed()`
2. File copied from DiskFS → RAMFS (one-time cost)
3. All subsequent operations use RAMFS (fast)
4. Writes propagate to DiskFS (write-through)

**Benefits:**
- Read performance: O(1) RAMFS access after initial load
- Transparent to applications
- Automatic caching

**Limitations:**
- Maximum 64KB file size for loading
- Entire file loaded (no partial loading)
- No eviction policy (files remain in RAMFS until reboot)

### Write-Through Caching

**Implementation:**
```c
int w = ramfs_write(path, buf, len, pos);  // Write to RAM
if (w > 0) {
    diskfs_create(path);                    // Ensure on disk
    diskfs_write(path, buf, len, pos);     // Write to disk
}
```

**Consistency Model:**
- Writes visible immediately in RAMFS
- Persisted immediately to DiskFS
- Crash-consistent (no dirty buffers)

**Performance Impact:**
- Write latency increased by disk I/O time
- No write coalescing or batching
- Every write blocks on disk operation

**Alternative (Not Implemented):** Write-back caching would batch writes, require periodic sync.

### File Descriptor Management

**Allocation:**
- Linear search for first free slot
- O(32) worst case (acceptable for 32 entries)
- No prioritization or optimization

**Global Table:**
- All file descriptors shared across kernel
- No per-process isolation (single-tasking kernel assumption)
- Potential conflict if multi-process support added

**Resource Limits:**
- Maximum 32 open files system-wide
- No per-process limits
- No quotas

### POSIX Compliance

**Implemented:**
- open/close/read/write/seek
- O_RDONLY, O_WRONLY, O_RDWR flags
- O_CREAT, O_TRUNC, O_APPEND
- SEEK_SET, SEEK_CUR, SEEK_END
- stat() for size and type

**Not Implemented:**
- dup/dup2 (file descriptor duplication)
- fcntl (file control operations)
- ioctl (device control)
- flock/lockf (file locking)
- mmap (memory mapping)
- readv/writev (vectored I/O)
- truncate/ftruncate (explicit size change)
- chmod/chown (permissions)
- symlink/readlink (symbolic links)

### Error Handling

**Common Errors:**
- `-1`: Generic failure (file not found, invalid fd, allocation failure)
- `0`: EOF on read
- Partial writes not distinguished from full failures

**No errno:** Error codes not set (simplified error model).

**Robustness:**
- Validates fd bounds and usage
- Checks file existence before operations
- Handles allocation failures gracefully

### Memory Management

**Allocations:**
- File descriptor table: Static array (no dynamic allocation)
- Temporary buffers: `kmalloc(65536)` for lazy loading
- Path strings: Copied into descriptor (128 bytes)

**Leaks:**
- Temporary buffers freed after use
- No leaks in normal operation

### Performance Characteristics

| Operation | Time Complexity | Disk I/O |
|-----------|----------------|----------|
| `files_open` (cached) | O(1) avg | 0 |
| `files_open` (not cached) | O(1) + disk read | 1 read (up to 64KB) |
| `files_close` | O(1) | 0 |
| `files_read` | O(1) + memcpy | 0 (from RAMFS) |
| `files_write` | O(1) + memcpy + disk | 1 write to disk |
| `files_seek` | O(1) | 0 |
| `files_stat` | O(1) | 0 if cached, 1 read if not |

**Bottleneck:** `files_write()` blocks on disk I/O (10-1000μs typical).

### Design Decisions

#### Why Lazy Loading?

**Problem:** Limited RAMFS capacity, large disk capacity.

**Solution:** Load files on-demand from disk to RAM.

**Benefits:**
- Boot time not proportional to disk contents
- Only used files consume RAM
- Transparent to applications

**Trade-off:** First access slower, but subsequent accesses fast.

#### Why Write-Through?

**Alternative:** Write-back caching (buffer writes, periodic flush).

**Choice:** Write-through ensures durability and simplifies crash recovery.

**Benefits:**
- Consistency after every write
- No need for explicit sync/flush
- Crash recovery simpler (no dirty buffers)

**Trade-off:** Slower write performance.

#### Why 64KB Max File Size?

**Hardcoded Buffer:**
```c
void *tmp = kmalloc(65536);  // 64KB buffer
```

**Rationale:**
- Embedded system with small files
- Simple implementation (no streaming)
- Reduces fragmentation

**Limitation:** Files > 64KB cannot be lazy-loaded from disk.

**Future:** Could implement chunked loading or increase buffer size.

#### Why Global FD Table?

**Current Model:** Single global table shared by entire kernel.

**Alternative:** Per-process file descriptor tables (like UNIX).

**Rationale:**
- Kernel is single-tasking (no process isolation needed)
- Simpler implementation
- Sufficient for embedded use case

**Future:** Multi-process support would require per-process tables.

## Usage Examples

### Basic File I/O

```c
// Initialize filesystem subsystems
files_init();
ramfs_init();
diskfs_init();

// Write file
int fd = files_open("/data.txt", O_WRONLY | O_CREAT);
files_write(fd, "Hello, World!\n", 14);
files_close(fd);

// Read file
fd = files_open("/data.txt", O_RDONLY);
char buf[100];
int n = files_read(fd, buf, sizeof(buf));
buf[n] = '\0';
printf("%s", buf);  // Prints: Hello, World!
files_close(fd);
```

### Append to Log File

```c
int log_fd = files_open("/system/log.txt", O_WRONLY | O_CREAT | O_APPEND);

void log_message(const char *msg) {
    files_write(log_fd, msg, strlen(msg));
}

log_message("System started\n");
log_message("User logged in\n");

files_close(log_fd);
```

### Binary File Processing

```c
// Read binary file
int fd = files_open("/image.bin", O_RDONLY);
struct file_stat st;
files_stat("/image.bin", &st);

uint8_t *data = malloc(st.size);
files_read(fd, data, st.size);
files_close(fd);

// Process data...
for (int i = 0; i < st.size; i++) {
    data[i] ^= 0xFF;  // Invert bits
}

// Write back
fd = files_open("/image.bin", O_WRONLY | O_TRUNC);
files_write(fd, data, st.size);
files_close(fd);

free(data);
```

### Seeking and Partial Writes

```c
// Create file with header and data
int fd = files_open("/datafile.bin", O_RDWR | O_CREAT);

// Write header at start
uint32_t header[4] = {0x12345678, 100, 200, 300};
files_write(fd, header, sizeof(header));

// Seek past header, write data
files_seek(fd, 16, SEEK_SET);
uint8_t data[100];
memset(data, 0xAA, sizeof(data));
files_write(fd, data, sizeof(data));

// Go back and update header field
files_seek(fd, 4, SEEK_SET);  // Offset 4 (second header field)
uint32_t new_value = 150;
files_write(fd, &new_value, sizeof(new_value));

files_close(fd);
```

### Checking File Existence

```c
struct file_stat st;
if (files_stat("/config.txt", &st) == 0) {
    printf("File exists, size: %zu\n", st.size);
} else {
    printf("File not found\n");
}
```

### System Call Interface

```c
// In syscall.c - expose file operations to userspace

int sys_open(const char *path, int flags) {
    // Validate path is userspace pointer
    if (!is_valid_user_ptr(path)) return -1;
    return files_open(path, flags);
}

int sys_read(int fd, void *buf, size_t len) {
    if (!is_valid_user_ptr(buf)) return -1;
    return files_read(fd, buf, len);
}

int sys_write(int fd, const void *buf, size_t len) {
    if (!is_valid_user_ptr(buf)) return -1;
    return files_write(fd, buf, len);
}

// etc.
```

## Cross-References

### Related Documentation

- **[ramfs.c.md](ramfs.c.md)** - Primary storage backend for files.c
- **[diskfs.c.md](diskfs.c.md)** - Persistent storage backend
- **[syscall.c.md](../core/syscall.c.md)** - System call interface exposing file operations
- **[init.c.md](../core/init.c.md)** - Initialization sequence

### Related Source Files

```
kernel/
├── files.c           # This implementation
├── files.h           # Public API header
├── ramfs.c           # Fast in-memory backend
├── diskfs.c          # Persistent disk backend
├── syscall.c         # Exposes files_* as syscalls
└── commands/
    ├── cat.c         # Uses file I/O via init wrappers
    ├── edit.c        # Text editor using files_*
    └── view.c        # File viewer
```

### API Usage

| Component | Functions Used | Purpose |
|-----------|---------------|---------|
| Userspace apps | All via syscalls | Standard file I/O |
| Kernel programs | Direct API calls | Internal file access |
| Shell commands | Via init.c wrappers | File manipulation |
| Service manager | open/read/write | Config file loading |

### Integration Layers

```
┌─────────────────────────────────────┐
│   Applications / Shell Commands     │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   System Calls (syscall.c)          │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   files.c - File Abstraction        │  ← This Layer
│   • File descriptors                │
│   • Lazy loading                    │
│   • Write-through caching           │
└──────┬───────────────────┬──────────┘
       │                   │
┌──────▼─────┐      ┌──────▼──────────┐
│   ramfs.c  │      │    diskfs.c     │
│   (fast)   │      │  (persistent)   │
└────────────┘      └──────┬──────────┘
                           │
                    ┌──────▼──────────┐
                    │    virtio.c     │
                    │  (block driver) │
                    └─────────────────┘
```

### Future Enhancements

Potential improvements for files.c:

1. **Larger File Support:**
   - Remove 64KB lazy loading limit
   - Implement streaming/chunked loading
   - Support files larger than available RAM

2. **Better Caching:**
   - LRU eviction policy for RAMFS
   - Partial file caching (hot blocks only)
   - Read-ahead for sequential access

3. **Write Optimization:**
   - Write-back caching with periodic flush
   - Batch multiple writes before disk sync
   - Async I/O with callbacks

4. **Per-Process FDs:**
   - Per-process file descriptor tables
   - fd inheritance on fork()
   - Close-on-exec flag support

5. **Advanced Features:**
   - dup/dup2 for fd duplication
   - File locking (flock/lockf)
   - Memory mapping (mmap)
   - Vectored I/O (readv/writev)
   - Async I/O (aio_read/aio_write)

6. **Error Handling:**
   - Detailed error codes (errno)
   - Better distinction between error types
   - Retry logic for transient failures

7. **Resource Management:**
   - Per-process fd limits
   - System-wide fd quotas
   - Graceful handling of fd exhaustion

8. **POSIX Compliance:**
   - fcntl() for file control
   - fstat() for fd-based stat
   - truncate/ftruncate()
   - File permissions checking

---

**Last Updated:** 2024  
**Maintainer:** MYRASPOS Kernel Team  
**Dependencies:** RAMFS, DiskFS, VirtIO  
**Thread Safety:** Not thread-safe (single-threaded kernel assumed)
