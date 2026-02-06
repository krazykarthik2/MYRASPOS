# Kernel Heap Allocator - kmalloc.c/h

## Overview

`kmalloc` is MYRASPOS's **variable-size memory allocator** for the kernel heap. It provides `malloc`/`free`-like functionality with **automatic coalescing** of adjacent free blocks to reduce fragmentation.

**Memory Hierarchy**:
```
kmalloc (variable-size: 16 bytes - MB)
    ↓
palloc (fixed 4KB pages)
    ↓
Physical RAM
```

## Architecture

### Design: Coalescing Free List Allocator

**Strategy**: Maintains an **address-ordered free list** that merges adjacent free blocks automatically.

**Key Features**:
- **Address-ordered**: Free blocks sorted by memory address
- **Coalescing**: Adjacent blocks merged immediately on free
- **Split on demand**: Large blocks split when smaller allocation requested
- **Double-free detection**: Warns if same block freed twice
- **16-byte alignment**: All allocations aligned for AArch64 SIMD/atomic ops

### Data Structures

#### Block Header

```c
struct km_header {
    size_t size;           /* Payload size (user data) */
    int is_large;          /* >0: page count for large alloc, 0: small block */
    struct km_header *next;/* Pointer to next free block */
    uint64_t padding;      /* Ensures 16-byte alignment */
};
```

**Size**: 32 bytes (ensures payload starts at 16-byte boundary)

**Memory Layout**:
```
┌────────────────────┐ ← Allocated pointer (user sees this)
│   Payload Data     │
│   (size bytes)     │
├────────────────────┤ ← (address - 32)
│   padding (8)      │
│   next (8)         │
│   is_large (4)     │
│   size (4)         │
└────────────────────┘ ← km_header start
```

#### Free List

```c
static struct km_header *free_list = NULL;
```

**Ordering**: Sorted by memory address (low to high)

**Example**:
```
free_list → [Block at 0x1000, 128B] → [Block at 0x3000, 256B] → [Block at 0x5000, 512B] → NULL
```

**Invariant**: `cur->next == NULL || cur < cur->next`

## API Reference

### kmalloc_init()

```c
void kmalloc_init(void);
```

**Purpose**: Initializes the heap allocator.

**Behavior**:
- Prints initialization message
- Free list starts empty (expanded on first allocation)

**Called from**: `kernel_main()` after `palloc_init()`

**Note**: Actual heap pages allocated lazily (on first `kmalloc()`)

### kmalloc()

```c
void *kmalloc(size_t size);
```

**Purpose**: Allocates `size` bytes from kernel heap.

**Parameters**:
- `size`: Requested allocation size (bytes)

**Returns**:
- Pointer to allocated memory (success)
- `NULL` (failure - out of memory)

**Guarantees**:
- 16-byte aligned
- Contiguous memory
- Not zeroed (unlike `palloc_alloc()`)

**Algorithm**:

```
1. If size == 0: return NULL
2. Round size up to 16-byte multiple
3. If size + header > PAGE_SIZE:
   → Large allocation path (use palloc_alloc_contig)
4. Else:
   → Small allocation path (free list)
```

#### Small Allocation Path

```
1. Scan free list for first-fit block (size >= requested)
2. If found:
   a. If block much larger (size + 48+ bytes extra):
      - Split block: [allocated][new free block]
   b. Else:
      - Use entire block (avoid tiny fragments)
   c. Remove from free list
   d. Return payload pointer
3. If not found:
   a. Expand heap (allocate page from palloc)
   b. Add page to free list
   c. Retry allocation
```

**Complexity**: O(n) where n = free list length

#### Large Allocation Path

```c
// size + header > 4096
size_t pages = (size + sizeof(struct km_header) + PAGE_SIZE - 1) / PAGE_SIZE;
void *ptr = palloc_alloc_contig(pages);
```

**Reason**: Allocations > ~4KB bypass free list entirely
- More efficient for large buffers
- Avoids fragmenting free list
- Direct palloc interface for multi-page blocks

**Example**:
```c
char *buffer = kmalloc(1024);   // 1KB - from free list
void *big = kmalloc(1024*1024); // 1MB - direct from palloc (257 pages)
```

### kfree()

```c
void kfree(void *ptr);
```

**Purpose**: Frees memory allocated by `kmalloc()`.

**Parameters**:
- `ptr`: Pointer returned by `kmalloc()`

**Behavior**:

```c
if (!ptr) return;  // NULL is safe to free
struct km_header *h = (struct km_header *)ptr - 1;

if (h->is_large > 0) {
    palloc_free(h, h->is_large);  // Free multiple pages
    return;
}
// Small block: insert into address-ordered free list
```

**Coalescing Algorithm**:

```
1. Find insertion point in free list (sorted by address)
2. Check for double-free (already in list)
3. Insert block
4. Coalesce with NEXT block if adjacent
5. Coalesce with PREVIOUS block if adjacent
```

**Adjacent Detection**:
```c
// Are blocks adjacent?
(uint8_t *)block1 + sizeof(header) + block1->size == (uint8_t *)block2
```

**Coalesce Example**:

```
Before free(B):
Free List: [A: 0x1000, 128B] → [C: 0x3000, 256B] → NULL
Allocated: [B: 0x1080, 64B]

After free(B):
Free List: [A: 0x1000, 224B] → [C: 0x3000, 256B] → NULL
                ↑ Merged A+B
```

**Thread Safety**: Uses `irq_save()`/`irq_restore()`

### krealloc()

```c
void *krealloc(void *ptr, size_t new_size);
```

**Purpose**: Resizes an allocation (like C `realloc()`).

**Parameters**:
- `ptr`: Existing allocation (or NULL)
- `new_size`: New size (or 0 to free)

**Returns**:
- Pointer to resized allocation (may be same or different address)
- `NULL` (failure or `new_size == 0`)

**Behavior**:

```c
if (!ptr) return kmalloc(new_size);         // Treat as new allocation
if (new_size == 0) { kfree(ptr); return NULL; }  // Treat as free

if (existing_size >= new_size) return ptr;  // Already big enough

// Need to grow: allocate new, copy old, free old
void *new_ptr = kmalloc(new_size);
memcpy(new_ptr, ptr, old_size);
kfree(ptr);
return new_ptr;
```

**Note**: Always copies data (no in-place expansion)

**Example**:
```c
char *buf = kmalloc(100);
// ... fill buffer ...
buf = krealloc(buf, 200);  // Expand to 200 bytes (copies old data)
```

## Implementation Details

### Memory Alignment

```c
size = (size + 15) & ~15;  // Round up to 16-byte multiple
```

**Why 16-byte alignment?**
1. **AArch64 SIMD**: NEON/SVE instructions require 16-byte alignment
2. **Atomic operations**: 128-bit atomics need 16-byte alignment
3. **Cache efficiency**: Cache lines are 64 bytes (multiples of 16)
4. **ABI compliance**: Procedure Call Standard requires 16-byte stack alignment

### Block Splitting

```c
if (cur->size >= size + sizeof(struct km_header) + 16) {
    // Split: create new free block from remainder
    struct km_header *new_block = (uint8_t *)cur + sizeof(struct km_header) + size;
    new_block->size = cur->size - size - sizeof(struct km_header);
    new_block->next = cur->next;
    cur->size = size;
    *prev = new_block;  // Insert new block in free list
}
```

**Threshold**: Only split if remainder > 48 bytes (header + 16B payload)

**Rationale**: Avoid creating tiny unusable fragments

### Coalescing

#### Forward Coalescing (merge with next block)

```c
if (h->next && (uint8_t *)h + sizeof(struct km_header) + h->size == (uint8_t *)h->next) {
    h->size += sizeof(struct km_header) + h->next->size;
    h->next = h->next->next;
}
```

**Effect**: Extends current block to absorb next block

#### Backward Coalescing (merge with previous block)

```c
struct km_header *p = free_list;
while (p && p->next != h) p = p->next;
if (p && (uint8_t *)p + sizeof(struct km_header) + p->size == (uint8_t *)h) {
    p->size += sizeof(struct km_header) + h->size;
    p->next = h->next;
}
```

**Effect**: Previous block extends to absorb current block

### Heap Expansion

```c
static void km_expand_small(void) {
    void *page = palloc_alloc();  // Get 4KB page
    struct km_header *h = (struct km_header *)page;
    h->size = PAGE_SIZE - sizeof(struct km_header);  // ~4064 bytes usable
    h->is_large = 0;
    kfree((void *)(h + 1));  // Add to free list (triggers coalescing)
}
```

**Trigger**: Called when free list has no suitable block

**Effect**: Adds 4KB to heap, immediately available for allocation

**Growth Strategy**: One page at a time (not exponential)

### Double-Free Detection

```c
while (cur && cur < h) {
    prev = &cur->next;
    cur = cur->next;
}
if (cur == h) {
    uart_puts("[kmalloc] WARNING: Double-free detected at ...");
    return;  // Ignore the free
}
```

**How**: Address-ordered list makes duplicate detection trivial

**Safety**: Prevents list corruption from double-free bugs

### Cycle Detection

```c
int limit = 10000;
while (cur && limit-- > 0) {
    // ... search free list ...
}
if (limit <= 0) {
    uart_puts("[kmalloc] FATAL: Free list cycle detected!\n");
    return NULL;
}
```

**Purpose**: Detect corrupted free list (circular links)

**Threshold**: 10,000 nodes (reasonable for 4KB pages)

## Design Decisions

### Why Address-Ordered Free List?

**Alternatives**:
- **LIFO (stack)**: Fast but no coalescing
- **Size-ordered**: Fast best-fit but no coalescing
- **Address-ordered**: Enables automatic coalescing ✓

**Trade-off**: Slower free() but much better fragmentation behavior

### Why Coalescing?

**Without Coalescing**:
```
Allocate 100B, Free, Allocate 100B, Free → Free List: [100B][100B]
Now allocate 200B → FAIL (no block big enough)
```

**With Coalescing**:
```
Same sequence → Free List: [200B] → 200B allocation succeeds
```

**Benefit**: Greatly extends effective heap size

### Why 16-Byte Alignment?

**Over-Alignment**:
- Wastes ~7.5 bytes per allocation on average
- But prevents crashes from misaligned SIMD/atomic ops
- AArch64 can trap on misaligned access (configurable)

**Decision**: Safety and performance over space

### Why Direct palloc for Large Allocations?

**Small allocations** (< 4KB):
- Use free list with coalescing
- Efficient for typical kernel allocations

**Large allocations** (> 4KB):
- Bypass free list entirely
- Go directly to page allocator
- Avoids fragmenting free list with huge blocks

**Threshold**: PAGE_SIZE - sizeof(header) ≈ 4064 bytes

## Performance Characteristics

### kmalloc()
- **Best Case**: O(1) - first free block fits
- **Average**: O(n) - scan partial free list
- **Worst Case**: O(n) + page allocation + O(n) retry

### kfree()
- **Large block**: O(1) - direct palloc_free()
- **Small block**: O(n) - scan to find insertion point + O(1) coalesce

### krealloc()
- **No resize needed**: O(1)
- **Resize needed**: O(n) allocate + O(m) copy + O(n) free

Where:
- n = free list length (~pages allocated / average block size)
- m = bytes to copy

## Memory Layout Example

```
Heap after several allocations:

Page 1 (0x50000000):
├─ [Header: 32B][Allocated: 100B] ← Task 1 buffer
├─ [Header: 32B][FREE: 256B]       ← In free list
├─ [Header: 32B][Allocated: 200B] ← String buffer
└─ [FREE: ~3500B]                  ← In free list

Page 2 (0x50001000):
├─ [Header: 32B][Allocated: 1024B] ← Network buffer
└─ [FREE: ~3000B]                   ← In free list

Large Allocation (0x50002000 - 0x50102000):
└─ [Header: 32B][Allocated: 1MB]   ← Framebuffer (256 pages)

Free List (address-ordered):
0x50000000+132 (256B) → 0x50000000+400 (3500B) → 0x50001000+1056 (3000B) → NULL
```

## Integration with Other Subsystems

### String Operations (lib.c)

```c
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = kmalloc(len);
    memcpy(copy, s, len);
    return copy;
}
```

### Task Management (sched.c)

```c
struct task *task_create(void (*func)(void)) {
    struct task *t = kmalloc(sizeof(struct task));
    t->stack = palloc_alloc_contig(4);  // Stack uses palloc directly
    // ...
}
```

### File System (ramfs.c, diskfs.c)

```c
struct file *file_create(const char *name) {
    struct file *f = kmalloc(sizeof(struct file));
    f->name = strdup(name);  // Uses kmalloc internally
    return f;
}
```

### Network Buffers (future)

```c
void *rx_buffer = kmalloc(1500);  // Ethernet MTU
// ... receive packet ...
kfree(rx_buffer);
```

## Error Handling

### Out of Memory

```c
void *ptr = kmalloc(size);
if (!ptr) {
    uart_puts("[subsystem] Allocation failed, graceful degradation\n");
    return -ENOMEM;
}
```

**Strategy**: Caller must check return value

**Kernel Behavior**:
- Critical allocations (page tables): System panic
- Non-critical (buffers): Return error to user

### Memory Leak Detection

```c
// Simple leak check (add to debug build):
static size_t total_allocated = 0;
static size_t total_freed = 0;

void kmalloc_stats(void) {
    uart_puts("Allocated: "); uart_put_hex(total_allocated);
    uart_puts(" Freed: "); uart_put_hex(total_freed);
    uart_puts(" Leaked: "); uart_put_hex(total_allocated - total_freed);
}
```

## Debugging

### Common Issues

**1. Memory Corruption**:
```
Symptom: Crash in kfree() or kmalloc()
Cause: Buffer overflow writing past allocation
Debug: Add guard bytes at end of each block
```

**2. Fragmentation**:
```
Symptom: kmalloc() fails despite free memory
Cause: Free list has many small blocks, none large enough
Debug: Print free list with block sizes
```

**3. Memory Leak**:
```
Symptom: Increasing memory usage over time
Cause: Allocated but never freed
Debug: Add allocation tracking per caller
```

### Debug Functions

```c
void kmalloc_dump_free_list(void) {
    struct km_header *cur = free_list;
    uart_puts("Free List:\n");
    while (cur) {
        uart_puts("  Block at "); uart_put_hex((uintptr_t)cur);
        uart_puts(" size="); uart_put_hex(cur->size);
        uart_puts(" next="); uart_put_hex((uintptr_t)cur->next);
        uart_puts("\n");
        cur = cur->next;
    }
}

void kmalloc_validate_free_list(void) {
    struct km_header *cur = free_list;
    while (cur && cur->next) {
        if (cur >= cur->next) {
            uart_puts("[kmalloc] CORRUPTION: List not address-ordered!\n");
            return;
        }
        cur = cur->next;
    }
    uart_puts("[kmalloc] Free list validated OK\n");
}
```

## Security Considerations

### Use-After-Free

**Vulnerability**: Accessing freed memory

**Mitigation**: 
- Debug builds can fill freed blocks with 0xDEADBEEF
- Not zeroed in release (performance)

### Double-Free

**Detection**: Implemented (checks if block already in free list)

**Protection**: Prevents list corruption, prints warning

### Heap Overflow

**Vulnerability**: Writing past allocation end

**Mitigation** (not implemented):
- Guard bytes (0xDEADBEEF) after each block
- Check guards in kfree()

### Integer Overflow

```c
size = (size + 15) & ~15;  // Could overflow if size near SIZE_MAX
```

**Fix** (add):
```c
if (size > SIZE_MAX - 15) return NULL;
```

## Cross-References

- **palloc**: [palloc.c](palloc.c.md) - Page allocator (kmalloc builds on this)
- **lib**: [lib.c](../lib/lib.c.md) - String functions use kmalloc
- **ramfs**: [ramfs.c](../fs/ramfs.c.md) - File metadata allocated via kmalloc
- **scheduler**: [03-TASK-SCHEDULING.md](../03-TASK-SCHEDULING.md) - Task structs allocated via kmalloc

## Future Enhancements

1. **Slab Allocator**: Cache frequently-used sizes (e.g., 32B, 64B, 128B)
2. **Per-CPU Heaps**: Reduce lock contention on multicore
3. **Memory Accounting**: Track allocations per task/subsystem
4. **Guard Pages**: Detect buffer overflows
5. **Lazy Coalescing**: Defer coalescing to reduce free() overhead
6. **Red-Black Tree**: O(log n) free list instead of linked list
7. **Allocation Profiling**: Identify hot allocation sizes
8. **Kernel Address Sanitizer (KASAN)**: Detect memory errors

## Historical Notes

The coalescing allocator design is inspired by Doug Lea's `dlmalloc` and Linux's early `kmalloc` implementation. The address-ordered free list is a classic technique from the 1960s that remains effective for systems without extreme memory pressure.
