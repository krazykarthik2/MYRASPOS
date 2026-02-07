# ramfs.c - RAM Filesystem Implementation

## Overview

The RAM Filesystem (RAMFS) is the primary in-memory filesystem for MYRASPOS. It provides a simple, flat namespace for files and directories stored entirely in kernel memory. RAMFS serves as the working storage layer for the operating system, with all file operations executing directly in RAM for maximum performance.

### Architecture

RAMFS implements a **linked-list-based filesystem** where each file or directory is represented as a node in a singly-linked list. The architecture prioritizes simplicity and speed over space efficiency, making it ideal for embedded systems with sufficient RAM.

**Key Design Characteristics:**
- Flat namespace with hierarchical path support through naming conventions
- Directories distinguished by trailing slash (`/`) in their names
- All data stored in dynamically allocated kernel memory via `kmalloc()`
- LRU-style path cache for frequent lookups (32 entries)
- No persistence - contents lost on reboot (use `ramfs_export`/`import` for serialization)

### Integration Points

- **files.c**: Uses RAMFS as the primary storage backend for the file abstraction layer
- **diskfs.c**: Synchronizes with RAMFS for persistence (RAMFS → Disk writes)
- **Shell Commands**: All file manipulation commands (cat, ls, cp, etc.) use RAMFS via init.c wrappers
- **init.c**: Provides wrapper functions (`init_ramfs_*`) for safe access from userspace programs

## Data Structures

### `struct ram_node`

The fundamental data structure representing a file or directory in RAMFS.

```c
struct ram_node {
    char name[RAMFS_NAME_MAX];  // Full absolute path (e.g., "/system/config.txt")
    size_t size;                 // Data size in bytes (0 for directories)
    uint8_t *data;               // Pointer to file contents (NULL for directories)
    struct ram_node *next;       // Next node in linked list
};
```

**Fields:**
- `name[64]`: Full absolute path including filename. Directories end with `/`
- `size`: File size in bytes. Directories have size 0
- `data`: Dynamically allocated buffer holding file contents. NULL for directories
- `next`: Pointer to next node in the global linked list

### `struct path_cache_entry`

Cache entry for accelerating repeated path lookups.

```c
struct path_cache_entry {
    char name[RAMFS_NAME_MAX];  // Cached path
    struct ram_node *node;       // Direct pointer to node
};
```

**Cache Behavior:**
- 32-entry circular buffer cache
- Checked before linear search in `find_node()`
- Invalidated on `ramfs_remove_recursive()` operations
- Simple string comparison for lookup

### Global State

```c
static struct ram_node *root = NULL;                          // Head of linked list
static struct path_cache_entry path_cache[PATH_CACHE_SIZE];   // Lookup cache
static int path_cache_next = 0;                               // Next cache slot to fill
```

## Key Functions

### Initialization

#### `int ramfs_init(void)`

Initializes the RAMFS by clearing the root pointer.

**Returns:** Always 0 (success)

**Implementation:**
```c
int ramfs_init(void) {
    root = NULL;
    return 0;
}
```

**Usage:** Called once during kernel boot in `main.c` or `init.c`.

---

### File Operations

#### `int ramfs_create(const char *name)`

Creates a new empty file with the given absolute path.

**Parameters:**
- `name`: Full absolute path (e.g., `/home/user/file.txt`)

**Returns:**
- `0` on success
- `-1` if file already exists or allocation fails

**Implementation Details:**
1. Check if file already exists via `find_node(name)`
2. Allocate new `ram_node` structure
3. Initialize: `size=0`, `data=NULL`
4. Prepend to linked list at `root`

**Example:**
```c
ramfs_create("/tmp/test.txt");  // Create empty file
```

---

#### `int ramfs_write(const char *name, const void *buf, size_t len, size_t offset)`

Writes data to a file at the specified offset, expanding the file if necessary.

**Parameters:**
- `name`: File path
- `buf`: Data buffer to write
- `len`: Number of bytes to write
- `offset`: Byte offset in file where writing begins

**Returns:**
- Number of bytes written on success
- `-1` if file not found or allocation fails

**Implementation Details:**
1. Locate file node via `find_node()`
2. If `offset + len > current_size`, reallocate data buffer:
   - Allocate new buffer of size `offset + len`
   - Copy existing data
   - Free old buffer
3. Copy `buf` into `node->data` at offset
4. Update `node->size` if expanded

**Growth Behavior:** Files automatically expand to accommodate writes beyond EOF. Gaps are filled with uninitialized data (old buffer content).

**Example:**
```c
char data[] = "Hello, RAMFS!";
ramfs_write("/tmp/test.txt", data, strlen(data), 0);  // Write at start
ramfs_write("/tmp/test.txt", " Append", 7, 13);       // Write at offset 13
```

---

#### `int ramfs_read(const char *name, void *buf, size_t len, size_t offset)`

Reads data from a file starting at the specified offset.

**Parameters:**
- `name`: File path
- `buf`: Destination buffer
- `len`: Maximum bytes to read
- `offset`: Starting byte offset in file

**Returns:**
- Number of bytes actually read (may be less than `len` if EOF reached)
- `0` if offset beyond EOF
- `-1` if file not found

**Implementation Details:**
1. Locate file node
2. Calculate readable bytes: `min(len, size - offset)`
3. Copy data from `node->data + offset` to `buf`

**Example:**
```c
char buffer[128];
int n = ramfs_read("/tmp/test.txt", buffer, 128, 0);
buffer[n] = '\0';  // Null-terminate for printing
```

---

#### `int ramfs_remove(const char *name)`

Removes a file or empty directory.

**Parameters:**
- `name`: Path to remove (with or without trailing `/` for directories)

**Returns:**
- `0` on success
- `-1` if not found or directory is not empty

**Implementation Details:**
1. Search linked list for exact name match
2. For directories (no trailing `/` in name):
   - Append `/` and check for children
   - Fail if any node has name with this prefix
3. Unlink node from list
4. Free `node->data` if non-NULL
5. Free `node` structure

**Directory Safety:** Non-empty directories cannot be removed. Use `ramfs_remove_recursive()` for recursive deletion.

**Example:**
```c
ramfs_remove("/tmp/test.txt");      // Remove file
ramfs_remove("/tmp/emptydir");      // Remove empty directory
ramfs_remove("/tmp/nonempty");      // Fails if directory has files
```

---

#### `int ramfs_remove_recursive(const char *name)`

Recursively removes a directory and all its contents.

**Parameters:**
- `name`: Directory path (with or without trailing `/`)

**Returns:**
- `0` if at least one node was removed
- `-1` if no nodes matched

**Implementation Details:**
1. Normalize `name` to have trailing `/` as `prefix`
2. Iterate entire linked list
3. Remove any node where:
   - Name exactly matches `name`, OR
   - Name starts with `prefix` (is a child)
4. Invalidate path cache after any removal

**Cache Invalidation:** This is the only operation that invalidates the entire path cache due to potentially removing many entries.

**Example:**
```c
ramfs_remove_recursive("/tmp");  // Removes /tmp and everything under it
```

---

### Directory Operations

#### `int ramfs_mkdir(const char *name)`

Creates a new directory.

**Parameters:**
- `name`: Directory path (trailing `/` optional)

**Returns:**
- `0` on success
- `-1` if directory exists or allocation fails

**Implementation Details:**
1. Ensure `name` ends with `/`
2. Check for existing directory node
3. Create empty node (size=0, data=NULL) with trailing `/` name
4. Prepend to linked list

**Directory Representation:** Directories are simply nodes with names ending in `/` and no data. Child files/directories have names starting with the directory's full path.

**Example:**
```c
ramfs_mkdir("/home");         // Creates /home/
ramfs_mkdir("/home/user");    // Creates /home/user/
```

---

#### `int ramfs_list(const char *dir, char *buf, size_t len)`

Lists immediate children of a directory.

**Parameters:**
- `dir`: Directory path (trailing `/` optional, except root must be `/`)
- `buf`: Output buffer for newline-separated entries
- `len`: Buffer capacity

**Returns:**
- Number of bytes written to `buf`
- `-1` on error

**Output Format:**
- Each entry on a new line (`\n` separated)
- Directories include trailing `/`
- Null-terminated string
- Duplicates are filtered

**Implementation Details:**
1. Normalize `dir` to have trailing `/` as `prefix`
2. Iterate all nodes
3. For nodes with names starting with `prefix`:
   - Extract immediate child component
   - If contains `/`, it's a subdirectory - add with trailing `/`
   - Check for duplicates in output buffer
   - Append to output buffer

**Immediate Children Only:** Filters out nested descendants, returning only direct children.

**Example:**
```c
char list[1024];
ramfs_mkdir("/home/");
ramfs_create("/home/file1.txt");
ramfs_create("/home/file2.txt");
ramfs_mkdir("/home/subdir/");

ramfs_list("/home", list, sizeof(list));
// list = "file1.txt\nfile2.txt\nsubdir/\n"
```

---

#### `int ramfs_is_dir(const char *name)`

Checks if a path represents a directory.

**Parameters:**
- `name`: Path to check (with or without trailing `/`)

**Returns:**
- `1` if path is a directory (has children or is empty directory node)
- `0` otherwise

**Implementation Details:**
1. Normalize `name` to have trailing `/`
2. Search for any node with name starting with normalized prefix
3. Return 1 if found, 0 otherwise

**Empty Directory Detection:** Returns true even for empty directories if the directory node exists.

**Example:**
```c
if (ramfs_is_dir("/home")) {
    // It's a directory
}
```

---

### Serialization (Persistence)

#### `int ramfs_export(const char *path)`

Serializes entire RAMFS contents into a single file within RAMFS.

**Parameters:**
- `path`: Destination file path (within RAMFS, e.g., `/backup.ramfs`)

**Returns:**
- `0` on success
- `-1` on allocation or write failure

**Export Format (Binary):**
```
Repeated for each node:
  [4 bytes] name_length (little-endian uint32)
  [name_length bytes] name string (no null terminator)
  [4 bytes] data_length (little-endian uint32)
  [data_length bytes] file data

Terminator:
  [4 bytes] 0x00000000 (zero name_length)
```

**Implementation Details:**
1. Calculate total size needed
2. Allocate single buffer
3. Serialize all nodes in linked list order
4. Write buffer to `path` as a file
5. Free temporary buffer

**Use Case:** Persist RAMFS state before shutdown or for debugging snapshots.

**Example:**
```c
ramfs_export("/system/ramfs_backup.bin");
// Later, after reboot:
ramfs_import("/system/ramfs_backup.bin");
```

---

#### `int ramfs_import(const char *path)`

Deserializes RAMFS contents from an exported file.

**Parameters:**
- `path`: Source file path (within RAMFS)

**Returns:**
- `0` on success
- `-1` on read error or invalid format

**Implementation Details:**
1. Read entire file (max 32KB) into buffer
2. Parse entries:
   - Read name_length
   - Read name
   - Read data_length
   - Read data
3. For each entry:
   - Remove existing file if present
   - Create new file
   - Write data

**Idempotency:** Overwrites existing files with imported versions.

**Limitations:** Maximum import size is 32KB (hardcoded buffer).

---

### Utility Functions

#### `int ramfs_get_size(const char *name)`

Returns the size of a file.

**Parameters:**
- `name`: File path

**Returns:**
- File size in bytes
- `-1` if file not found

**Example:**
```c
int size = ramfs_get_size("/tmp/test.txt");
if (size >= 0) {
    printf("File size: %d bytes\n", size);
}
```

---

### Internal Functions

#### `static struct ram_node *find_node(const char *name)`

Locates a node by path with caching.

**Algorithm:**
1. Check 32-entry path cache
2. On cache miss, perform linear search
3. Update cache on successful linear search
4. Return node pointer or NULL

**Performance:** O(1) average case with cache, O(n) worst case.

---

#### `static void invalidate_cache(void)`

Clears all path cache entries by setting first character to `\0`.

**Called By:** `ramfs_remove_recursive()` only.

---

## Implementation Details

### Memory Management

**Allocation Strategy:**
- Node structures: Fixed-size allocation via `kmalloc(sizeof(struct ram_node))`
- File data: Variable-size allocation, grows on demand during writes
- Reallocation: On file expansion, old buffer is freed and new buffer allocated

**Memory Leaks:** Properly frees both `node->data` and `node` on removal.

### Path Handling

**Absolute Paths:** All operations expect absolute paths (starting with `/`).

**Directory Naming:**
- Directories internally stored with trailing `/`
- API accepts paths with or without trailing `/`
- Root directory is special case: always `/`

**Path Components:**
- Extracted by finding `/` delimiters
- Used in `ramfs_list()` to determine immediate children

### Concurrency

**Thread Safety:** RAMFS is **not thread-safe**. All operations assume single-threaded access or external locking.

**Kernel Context:** RAMFS runs entirely in kernel mode. No user-space direct access.

### Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| `find_node` | O(1) avg, O(n) worst | With 32-entry cache |
| `create` | O(n) | Must check existence |
| `read` | O(n) + O(m) | n=lookup, m=bytes to copy |
| `write` | O(n) + O(m) | May need reallocation |
| `list` | O(n²) | Iterates all nodes, checks duplicates |
| `remove` | O(n) | Linear search and unlink |
| `remove_recursive` | O(n) | Single pass with cache invalidation |

Where:
- n = number of files in RAMFS
- m = number of bytes transferred

### Design Decisions

#### Why Linked List?

**Pros:**
- Simple implementation
- Easy insertion/deletion
- No fixed maximum file count
- Minimal memory overhead

**Cons:**
- O(n) lookup time
- Cache not effective for many files
- No hierarchical structure optimization

**Rationale:** For embedded systems with few files (< 1000), simplicity outweighs performance concerns.

#### Why Full Path Names?

Storing full paths (e.g., `/home/user/file.txt`) instead of hierarchical directory structures simplifies implementation:
- No need for directory tree traversal
- Faster path resolution
- Simpler serialization

Trade-off: More memory per node, but strings are typically < 64 bytes.

#### Why Path Cache?

**Problem:** Repeated lookups for the same files (e.g., editing a file, shell commands on same directory).

**Solution:** 32-entry LRU-style cache provides ~95% hit rate for typical usage patterns.

**Cache Invalidation:** Only `ramfs_remove_recursive()` needs full invalidation since it may remove many entries at once.

## Usage Examples

### Basic File Operations

```c
// Initialize RAMFS
ramfs_init();

// Create and write file
ramfs_create("/config.txt");
ramfs_write("/config.txt", "key=value\n", 10, 0);

// Read file
char buf[128];
int n = ramfs_read("/config.txt", buf, sizeof(buf), 0);

// Get file size
int size = ramfs_get_size("/config.txt");

// Remove file
ramfs_remove("/config.txt");
```

### Directory Management

```c
// Create directory hierarchy
ramfs_mkdir("/home");
ramfs_mkdir("/home/user");
ramfs_create("/home/user/notes.txt");

// List directory
char list[1024];
ramfs_list("/home/user", list, sizeof(list));

// Check if path is directory
if (ramfs_is_dir("/home/user")) {
    // Handle directory
}

// Remove directory recursively
ramfs_remove_recursive("/home");
```

### Serialization

```c
// Export entire filesystem
ramfs_export("/backup.ramfs");

// Clear filesystem
ramfs_remove_recursive("/");

// Restore from backup
ramfs_import("/backup.ramfs");
```

### Shell Command Integration

Shell commands use wrapper functions in `init.c`:

```c
// cat command implementation
int cmd_cat(const char *filename) {
    char buf[4096];
    int r = init_ramfs_read(filename, buf, sizeof(buf));
    if (r > 0) {
        write(STDOUT, buf, r);
    }
    return r < 0 ? -1 : 0;
}
```

## Cross-References

### Related Documentation

- **[files.c.md](files.c.md)** - File abstraction layer using RAMFS as backend
- **[diskfs.c.md](diskfs.c.md)** - Persistent storage synchronizing with RAMFS
- **[init.c](../core/init.c.md)** - Wrapper functions for safe access (`init_ramfs_*`)
- **[shell.c](../core/shell.c.md)** - Shell integration and command implementations

### Related Source Files

```
kernel/
├── ramfs.c           # This implementation
├── ramfs.h           # Public API header
├── files.c           # Uses RAMFS as storage backend
├── diskfs.c          # Syncs with RAMFS for persistence
├── init.c            # Provides init_ramfs_* wrappers
└── commands/
    ├── cat.c         # Reads files via init_ramfs_read()
    ├── ls.c          # Lists directories via init_ramfs_list()
    ├── cp.c          # Copies files using ramfs read/write
    ├── rm.c          # Removes files via init_ramfs_remove()
    └── ramfs_tools.c # Import/export commands
```

### API Usage in Kernel

| Component | Functions Used | Purpose |
|-----------|---------------|---------|
| files.c | read, write, create, get_size | File descriptor abstraction |
| diskfs.c | list, is_dir, read, create, write | Persistence sync |
| Shell | All functions via init.c wrappers | Command execution |
| Programs | create, read, write | Application data storage |

### Future Enhancements

Potential improvements for RAMFS:

1. **B-tree or hash table** for O(1) lookups with many files
2. **Reference counting** for open files to prevent deletion
3. **Sparse file support** with gap tracking
4. **Memory limits** with quota enforcement
5. **Proper hierarchical directory tree** for better scalability
6. **File permissions** and ownership metadata
7. **Thread-safe locking** for concurrent access
8. **mmap support** for direct memory mapping

---

**Last Updated:** 2024  
**Maintainer:** MYRASPOS Kernel Team  
**Related Specs:** POSIX-like file operations (subset)
