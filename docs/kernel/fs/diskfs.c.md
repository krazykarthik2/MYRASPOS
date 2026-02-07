# diskfs.c - Disk Filesystem with VirtIO Backend

## Overview

DiskFS is the persistent storage filesystem for MYRASPOS, providing durable storage backed by a VirtIO block device. It implements a simple sector-based filesystem that synchronizes with RAMFS to ensure data persists across system reboots.

### Architecture

DiskFS implements a **directory-table filesystem** with fixed metadata allocation and sequential data storage. The architecture prioritizes simplicity and boot-time loading over sophisticated features like journaling or wear-leveling.

**Key Design Characteristics:**
- Fixed directory table supporting up to 128 files
- 512-byte sector-based I/O via VirtIO block device driver
- Directory cached entirely in RAM for fast lookups
- Sequential sector allocation (no fragmentation handling)
- Write-through synchronization with RAMFS via files.c
- No subdirectory support (flat namespace)

### VirtIO Integration

DiskFS relies on the VirtIO block device driver (`virtio.c`) for all disk I/O operations. VirtIO provides a standardized paravirtualized interface for block devices in QEMU and other hypervisors.

**Interaction Model:**
1. DiskFS calls `virtio_blk_rw(sector, buffer, write_flag)`
2. VirtIO driver submits request to virtqueue
3. Hypervisor performs actual disk I/O
4. VirtIO returns data/status to DiskFS

### Integration Points

- **files.c**: Write-through caching - writes to RAMFS propagate to DiskFS
- **ramfs.c**: DiskFS loads files into RAMFS at boot, syncs from RAMFS on shutdown
- **virtio.c**: All disk I/O goes through VirtIO block device driver
- **init.c**: DiskFS initialization called during kernel startup

## Data Structures

### `struct disk_entry`

Represents a file's metadata in the on-disk directory table.

```c
struct disk_entry {
    char name[64];          // Full absolute path (e.g., "/system/config.txt")
    uint32_t size;          // File size in bytes
    uint32_t start_sector;  // First sector of file data
};
```

**Fields:**
- `name[64]`: Full path including filename (null-terminated)
- `size`: File size in bytes (0 for empty files)
- `start_sector`: Sector number where file data begins

**On-Disk Location:** Directory table starts at sector 1, occupies multiple sectors.

### Directory Table Layout

```c
static struct disk_entry dir_cache[MAX_DISK_FILES];  // 128 entries
static int num_files = 0;                            // Count of valid entries
static uint32_t next_free_sector = DATA_START_SECTOR; // Next available data sector
```

**Memory:**
- `dir_cache`: In-memory copy of on-disk directory
- Loaded from disk at `diskfs_init()`
- Written back to disk after modifications

### `struct disk_path_cache`

Path lookup cache for frequently accessed files.

```c
struct disk_path_cache {
    char name[64];      // Cached path
    int index;          // Index into dir_cache array
};
```

**Cache Behavior:**
- 16-entry circular buffer cache
- Avoids linear search through 128-entry directory
- Simple string comparison lookup
- Updated on successful `find_file_index()` hits

### Disk Layout

```
Sector 0:           [Reserved - could be boot sector/superblock]
Sectors 1-127:      Directory table (struct disk_entry array)
Sectors 128+:       File data (sequential allocation)
```

**Constants:**
```c
#define SECTOR_SIZE 512
#define DIR_START_SECTOR 1
#define DATA_START_SECTOR 128
#define MAX_DISK_FILES 128
```

**Directory Table Size:** `128 * sizeof(struct disk_entry) = 128 * 72 = 9216 bytes ≈ 18 sectors`

## Key Functions

### Initialization

#### `void diskfs_init(void)`

Initializes DiskFS by initializing VirtIO block device and loading directory from disk.

**Implementation Details:**
1. Initialize VirtIO block device via `virtio_blk_init()`
2. If VirtIO not available, disable DiskFS and return
3. Calculate sectors needed for directory table
4. Read directory sectors from disk into `dir_cache`
5. Count valid files (non-empty name field)
6. Calculate `next_free_sector` by finding highest used sector + size

**Boot Logging:**
```c
uart_puts("[diskfs] initialized. files found="); 
uart_put_hex(num_files); 
uart_puts("\n");
```

**Failure Handling:** If VirtIO unavailable, DiskFS silently disables itself. System continues with RAMFS only.

**Example:**
```c
// Called during kernel initialization
diskfs_init();
```

---

### File Operations

#### `int diskfs_create(const char *name)`

Creates a new empty file on disk or returns success if already exists.

**Parameters:**
- `name`: Full absolute path (e.g., `/config.txt`)

**Returns:**
- `0` on success or if file already exists
- `-1` if directory table full or other error

**Implementation Details:**
1. Check if file already exists via `find_file_index()`
2. If exists, return 0 (idempotent create)
3. Find first free slot in `dir_cache` (name[0] == '\0')
4. Initialize entry:
   - Copy name
   - Set size = 0
   - Set start_sector = next_free_sector
5. Increment num_files
6. Write directory table to disk via `save_dir()`

**Idempotency:** Creating an existing file is a no-op, returns success.

**Example:**
```c
diskfs_create("/system/log.txt");  // Creates new file
diskfs_create("/system/log.txt");  // No-op, returns 0
```

---

#### `int diskfs_write(const char *name, const void *buf, size_t len, size_t offset)`

Writes data to a file at the specified offset, updating file size if expanded.

**Parameters:**
- `name`: File path
- `buf`: Data to write
- `len`: Number of bytes to write
- `offset`: Byte offset in file

**Returns:**
- Number of bytes written (`len`) on success
- `-1` if file not found

**Implementation Details:**

1. Locate file via `find_file_index()`
2. Calculate starting sector: `start_s = start_sector + (offset / SECTOR_SIZE)`
3. For each sector to write:
   - **Full sector write:** Call `virtio_blk_rw(sector, data, 1)` directly
   - **Partial sector write:** 
     - Read existing sector
     - Modify in-memory buffer
     - Write modified sector back
4. Update file size if `offset + len > current_size`
5. Update `next_free_sector` if file expanded beyond previous allocation
6. Save directory table to disk

**Sector Alignment:** Handles unaligned writes by read-modify-write for partial sectors.

**File Expansion:** Automatically grows file and updates metadata when writing beyond EOF.

**Persistence:** Each write immediately syncs to disk (write-through).

**Example:**
```c
char data[] = "Hello, Disk!";
diskfs_write("/system/log.txt", data, strlen(data), 0);

// Append at offset
diskfs_write("/system/log.txt", "\nAppended", 9, strlen(data));
```

---

#### `int diskfs_read(const char *name, void *buf, size_t len, size_t offset)`

Reads data from a file starting at the specified offset.

**Parameters:**
- `name`: File path
- `buf`: Destination buffer
- `len`: Maximum bytes to read
- `offset`: Starting byte offset

**Returns:**
- Number of bytes read (may be less than `len` if EOF)
- `0` if offset >= file size
- `-1` if file not found

**Implementation Details:**

1. Locate file via `find_file_index()`
2. Clamp read length if offset + len exceeds file size
3. Calculate starting sector and skip offset within first sector
4. For each sector:
   - Read full sector into temporary buffer
   - Copy relevant portion to destination
   - Advance to next sector
5. Handle first sector skip and last sector partial read

**Sector Buffer:** Uses stack-allocated 512-byte buffer for sector staging.

**Boundary Handling:** Correctly handles reads spanning multiple sectors with arbitrary alignment.

**Example:**
```c
char buffer[1024];
int n = diskfs_read("/system/log.txt", buffer, sizeof(buffer), 0);
if (n > 0) {
    buffer[n] = '\0';
    printf("Read: %s\n", buffer);
}
```

---

### Directory Operations

#### `int diskfs_list(const char *dir, char *buf, size_t len)`

**Note:** Currently **not implemented** in the source code. Function signature exists in header but no implementation body.

**Expected Behavior:**
- Would list files in directory
- Return newline-separated file list
- Similar to `ramfs_list()`

**Limitations:** DiskFS currently has no directory listing implementation. Use RAMFS for directory operations.

---

### Synchronization Operations

#### `void diskfs_sync_from_ramfs(void)`

Synchronizes all files from RAMFS to DiskFS (backup operation).

**Purpose:** Persist RAMFS contents to disk before shutdown or for periodic backups.

**Implementation Details:**

1. Call `ramfs_list("/", ...)` to enumerate all files
2. For each file in RAMFS:
   - Skip directories (check `ramfs_is_dir()`)
   - Allocate temporary 64KB buffer
   - Read file from RAMFS
   - If file doesn't exist in DiskFS:
     - Log new file creation
     - Call `diskfs_create()`
     - Call `diskfs_write()` to copy data
   - Free temporary buffer

**Logging:**
```c
uart_puts("[diskfs] syncing from ramfs...\n");
uart_puts("  syncing NEW: /system/config.txt\n");
uart_puts("[diskfs] sync complete.\n");
```

**Limitations:**
- Maximum 64KB per file (hardcoded buffer size)
- Only syncs NEW files, doesn't update existing files
- No directory structure preservation

**Example:**
```c
// Before shutdown
diskfs_sync_from_ramfs();  // Persist all RAMFS files to disk
```

---

#### `void diskfs_sync_to_ramfs(void)`

Loads all files from DiskFS into RAMFS (restore operation).

**Purpose:** Populate RAMFS with persistent files at boot time.

**Implementation Details:**

1. Iterate through all 128 `dir_cache` entries
2. For each valid entry (name[0] != '\0'):
   - Log file discovery
   - Allocate buffer for entire file
   - Call `diskfs_read()` to load file
   - Parse path to create parent directories:
     - Walk path separators
     - Call `ramfs_mkdir()` for each component
   - Call `ramfs_create()` to create file
   - Call `ramfs_write()` to populate data
   - Free buffer

**Logging:**
```c
uart_puts("[diskfs] loading from disk to ramfs...\n");
uart_puts("[diskfs] found file on disk: /system/config.txt\n");
uart_puts("  loaded: /system/config.txt\n");
uart_puts("[diskfs] load complete.\n");
```

**Directory Creation:** Automatically creates parent directories by parsing path components.

**Error Handling:** Logs failures but continues processing remaining files.

**Example:**
```c
// At kernel boot
diskfs_init();
diskfs_sync_to_ramfs();  // Load persistent files into working RAMFS
```

---

### Internal Functions

#### `static int find_file_index(const char *name)`

Locates a file in the directory cache by name.

**Algorithm:**
1. Check 16-entry path cache
2. On cache miss, linear search through `dir_cache[128]`
3. Update cache on successful search
4. Return index (0-127) or -1 if not found

**Performance:** O(1) with cache hit, O(128) worst case.

**Example:**
```c
int idx = find_file_index("/system/config.txt");
if (idx >= 0) {
    uint32_t size = dir_cache[idx].size;
}
```

---

#### `static void save_dir(void)`

Writes the directory table from RAM to disk.

**Implementation:**
1. Calculate number of sectors needed for `dir_cache`
2. For each sector:
   - Call `virtio_blk_rw(sector, buffer, 1)` to write
3. Sequential writes starting at `DIR_START_SECTOR`

**When Called:**
- After `diskfs_create()`
- After `diskfs_write()` if file size changes

**Critical:** Ensures directory metadata persists after every modification.

**Example:**
```c
// Internal use only
dir_cache[idx].size = new_size;
save_dir();  // Persist change to disk
```

---

## VirtIO Integration

### VirtIO Block Device Driver

DiskFS relies entirely on the VirtIO block device driver for disk I/O. VirtIO provides a standardized paravirtualized interface between the guest OS and hypervisor.

#### `int virtio_blk_init(void)`

Initializes the VirtIO block device.

**Called By:** `diskfs_init()`

**Returns:**
- `0` on success (block device found and initialized)
- `-1` if no VirtIO block device available

**Implementation Details:**
- Probes MMIO region for VirtIO block device
- Initializes virtqueues for request submission
- Negotiates feature flags with device
- Returns failure if no block device found

---

#### `int virtio_blk_rw(uint64_t sector, void *buf, int write)`

Performs a synchronous single-sector read or write operation.

**Parameters:**
- `sector`: Sector number (512 bytes per sector)
- `buf`: 512-byte buffer for read/write data
- `write`: 0 for read, 1 for write

**Returns:**
- `0` on success
- `-1` on error

**Implementation Details:**
1. Construct VirtIO block request:
   ```c
   struct virtio_blk_req {
       uint32_t type;     // VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write)
       uint32_t reserved;
       uint64_t sector;   // Sector number
   };
   ```
2. Submit request to virtqueue
3. Wait for completion (synchronous operation)
4. Check status byte
5. Copy data from/to buffer

**Sector Size:** Fixed at 512 bytes (industry standard).

**Synchronous:** Blocks until I/O completes (no async operations).

**Example:**
```c
uint8_t sector_data[512];

// Read sector 100
virtio_blk_rw(100, sector_data, 0);

// Modify data
sector_data[0] = 0xFF;

// Write back
virtio_blk_rw(100, sector_data, 1);
```

---

### VirtIO Request Flow

```
diskfs_write() 
    ↓
virtio_blk_rw(sector, buf, 1)
    ↓
[Build virtio_blk_req structure]
    ↓
[Submit to virtqueue]
    ↓
[Notify hypervisor via MMIO write]
    ↓
[QEMU/KVM handles physical I/O]
    ↓
[Wait for completion interrupt]
    ↓
[Check status]
    ↓
Return to diskfs_write()
```

**Latency:** Each sector I/O incurs hypervisor exit/entry overhead (~1-10μs in QEMU).

**Optimization Opportunity:** Batching multiple sector operations would improve performance.

---

## Implementation Details

### Sector-Based I/O

**Why 512-byte Sectors?**
- Industry standard block size
- VirtIO spec defines 512-byte logical blocks
- Hardware compatibility (even with 4KB physical sectors)

**Alignment Handling:**

DiskFS handles arbitrary byte offsets by:
1. Calculating sector boundary: `sector = offset / 512`
2. Reading partial first/last sectors
3. Modifying in-memory copy
4. Writing full sectors back

**Read-Modify-Write:**
```c
// Writing 100 bytes at offset 200 (spans 2 sectors)
// Sector 0: bytes 0-511
// Sector 1: bytes 512-1023

virtio_blk_rw(0, sector_buf, 0);           // Read sector 0
memcpy(sector_buf + 200, data, 312);        // Modify bytes 200-511
virtio_blk_rw(0, sector_buf, 1);           // Write back

virtio_blk_rw(1, sector_buf, 0);           // Read sector 1
memcpy(sector_buf, data + 312, 88);        // Modify bytes 0-87
virtio_blk_rw(1, sector_buf, 1);           // Write back
```

### Sequential Allocation

**Allocation Strategy:**
- Files allocated in contiguous sectors
- `next_free_sector` tracks highest used sector + 1
- No reclamation of freed space (no deletion support)

**Fragmentation:**
- Internal fragmentation: Last sector of file may be partially used
- No external fragmentation (contiguous allocation)

**Growth:** Files can grow by writing beyond EOF. Sectors appended sequentially.

**Limitation:** No file deletion implemented, so space is never reclaimed.

### Write-Through Caching

Files.c implements write-through caching where writes go to both RAMFS and DiskFS:

```c
// In files.c:files_write()
int w = ramfs_write(path, buf, len, pos);
if (w > 0) {
    diskfs_create(path);           // Ensure exists on disk
    diskfs_write(path, buf, len, pos);  // Write through to disk
}
```

**Benefits:**
- RAMFS provides fast read performance
- DiskFS ensures persistence
- Consistency maintained automatically

**Cost:** Every write performs disk I/O (slow).

### Flat Namespace

**Limitation:** DiskFS doesn't support subdirectories in its native operations.

**Workaround:** Files stored with full paths like `/system/config.txt`, but DiskFS treats them as flat names.

**Directory Creation:** `diskfs_sync_to_ramfs()` parses paths and creates RAMFS directories, but DiskFS itself has no directory structure.

### Concurrency

**Thread Safety:** DiskFS is **not thread-safe**. Assumes single-threaded kernel or external locking.

**VirtIO:** VirtIO block operations are synchronous, blocking until completion.

### Performance Characteristics

| Operation | Time Complexity | I/O Operations |
|-----------|----------------|----------------|
| `find_file_index` | O(1) avg, O(128) worst | 0 |
| `create` | O(128) + disk I/O | ~18 sector writes (directory) |
| `read` | O(128) + disk I/O | ceil(len/512) sector reads |
| `write` | O(128) + disk I/O | ceil(len/512) sector writes + ~18 directory writes |
| `save_dir` | O(1) | ~18 sector writes |
| `diskfs_init` | O(1) | ~18 sector reads |

**Bottleneck:** VirtIO block I/O latency dominates (especially in nested virtualization).

### Design Decisions

#### Why Fixed Directory Table?

**Pros:**
- Simple implementation
- Fast initialization (read fixed sectors)
- Predictable memory usage
- No dynamic allocation complexity

**Cons:**
- Maximum 128 files limit
- Wasted space if few files
- No directory hierarchy

**Rationale:** Embedded systems typically have < 100 files. Simplicity preferred.

#### Why Sequential Allocation?

**Pros:**
- No fragmentation management needed
- Simple next_free_sector tracking
- Fast allocation (no search)

**Cons:**
- No space reclamation
- Files can't shrink efficiently
- Disk fills up permanently

**Rationale:** For small embedded storage (< 1MB typical), fragmentation not a concern. Deletion feature not implemented.

#### Why No Deletion?

**Current State:** DiskFS has no `diskfs_remove()` function.

**Workaround:** Files can be overwritten but not removed.

**Future:** Would require free space bitmap or linked free list.

#### Why Write-Through?

**Alternative:** Write-back caching (periodic flush to disk).

**Choice:** Write-through ensures consistency and simplifies crash recovery.

**Trade-off:** Performance cost on every write.

## Usage Examples

### Basic Disk Operations

```c
// Initialize disk filesystem
diskfs_init();

// Create file on disk
diskfs_create("/persistent.txt");

// Write data
char data[] = "Survives reboot";
diskfs_write("/persistent.txt", data, strlen(data), 0);

// Read back
char buffer[128];
int n = diskfs_read("/persistent.txt", buffer, sizeof(buffer), 0);
```

### Boot Sequence

```c
// In kernel initialization (init.c or main.c)

// 1. Initialize RAMFS (working storage)
ramfs_init();

// 2. Initialize DiskFS (persistent storage)
diskfs_init();

// 3. Load files from disk into RAM
diskfs_sync_to_ramfs();

// Now RAMFS contains all persistent files from disk
// System operates primarily on RAMFS for speed
```

### Shutdown Sequence

```c
// Before system shutdown

// Sync any new/modified RAMFS files to disk
diskfs_sync_from_ramfs();

// Now disk contains all files from RAMFS
// Safe to shutdown - data persists
```

### Integration with files.c

```c
// Files.c automatically handles disk persistence:

int fd = files_open("/config.txt", O_WRONLY | O_CREAT);
files_write(fd, "data", 4);  // Writes to RAMFS AND DiskFS
files_close(fd);

// After reboot:
diskfs_sync_to_ramfs();      // Loads /config.txt back from disk
int fd2 = files_open("/config.txt", O_RDONLY);
char buf[4];
files_read(fd2, buf, 4);     // Reads "data" from RAMFS
```

## Cross-References

### Related Documentation

- **[ramfs.c.md](ramfs.c.md)** - In-memory filesystem that DiskFS synchronizes with
- **[files.c.md](files.c.md)** - File abstraction layer with write-through to DiskFS
- **[virtio.c.md](../drivers/virtio.c.md)** - VirtIO block device driver
- **[init.c.md](../core/init.c.md)** - Kernel initialization sequence

### Related Source Files

```
kernel/
├── diskfs.c          # This implementation
├── diskfs.h          # Public API
├── virtio.c          # VirtIO block driver
├── virtio.h          # VirtIO interfaces
├── ramfs.c           # Synchronized in-memory filesystem
├── files.c           # Write-through caching layer
└── init.c            # Boot-time disk loading
```

### API Usage

| Component | Functions Used | Purpose |
|-----------|---------------|---------|
| files.c | create, write | Write-through persistence |
| init.c | init, sync_to_ramfs | Boot-time loading |
| Shell | sync_from_ramfs | Manual sync command |

### Future Enhancements

Potential improvements for DiskFS:

1. **File Deletion:**
   - Implement `diskfs_remove()`
   - Free space bitmap or linked free list
   - Sector reclamation on deletion

2. **Directory Support:**
   - Hierarchical directory structure
   - Separate inode and directory entry tables
   - `diskfs_list()` implementation

3. **Better Allocation:**
   - Extent-based allocation (reduce fragmentation)
   - Dynamic extent growth
   - Free space management

4. **Metadata:**
   - Timestamps (created, modified, accessed)
   - File permissions
   - Owner/group IDs

5. **Performance:**
   - Batch sector operations (reduce hypervisor exits)
   - Async I/O with completion callbacks
   - Read-ahead caching
   - Write coalescing

6. **Reliability:**
   - Checksums for data integrity
   - Journaling for crash consistency
   - Bad sector remapping
   - Redundant directory copies

7. **Capacity:**
   - Support > 128 files (dynamic directory)
   - Large file support (> 64KB)
   - Disk space quotas

8. **POSIX Compliance:**
   - Hard links
   - Symbolic links
   - File locking
   - Extended attributes

---

**Last Updated:** 2024  
**Maintainer:** MYRASPOS Kernel Team  
**Dependencies:** VirtIO block device, RAMFS  
**Limitations:** 128 files max, no deletion, flat namespace
