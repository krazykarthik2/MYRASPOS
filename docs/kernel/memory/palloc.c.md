# Page Allocator - palloc.c/h

## Overview

`palloc` (Page Allocator) is MYRASPOS's **physical memory manager** that allocates and frees memory in fixed-size 4KB pages. It uses a **bitmap-based allocation strategy** where each bit represents one page (0=free, 1=used).

**Role in Memory Hierarchy**:
```
Applications
     ↓
kmalloc (variable-size allocator)
     ↓
palloc (4KB page allocator) ← This file
     ↓
Physical RAM
```

## Architecture

### Data Structures

#### Global State (palloc.c)

```c
static size_t total_pages = 0;                        // Total pages in pool
static uint8_t *pool_start_addr = NULL;               // Start of managed memory
static uint8_t page_bitmap[PALLOC_MAX_PAGES / 8];    // Bitmap: 1 bit per page
```

**Memory Overhead**: 
- 256MB RAM → 65,536 pages → 8,192 bytes bitmap (~0.003% overhead)
- 1GB RAM → 262,144 pages → 32,768 bytes bitmap (~0.003% overhead)

**Max Capacity**: `PALLOC_MAX_PAGES = 256K pages = 1GB`

### Bitmap Structure

```
Byte 0: [P7][P6][P5][P4][P3][P2][P1][P0]  Pages 0-7
Byte 1: [P15][P14]...[P8]                 Pages 8-15
...
Byte N: [...][...][...][...]              Pages N*8 to N*8+7
```

- **0 bit**: Page is free
- **1 bit**: Page is in use

## API Reference

### palloc_init()

```c
void palloc_init(void *pool_start, size_t pages);
```

**Purpose**: Initializes the page allocator with a memory pool.

**Parameters**:
- `pool_start`: Physical address of the first page to manage
- `pages`: Number of 4KB pages in the pool

**Behavior**:
1. Caps `pages` at `PALLOC_MAX_PAGES` (256K) if exceeded
2. Stores pool start address
3. Clears entire bitmap (marks all pages free)
4. Logs configuration via UART

**Called from**: `kernel_main()` during early boot, after MMU initialization

**Example**:
```c
// Make 128MB available for allocation starting at 0x50000000
palloc_init((void *)0x50000000, (128 * 1024 * 1024) / PAGE_SIZE);
```

**Design Constraint**: Must be called before any allocations; calling twice will reset allocator state.

### palloc_alloc()

```c
void *palloc_alloc(void);
```

**Purpose**: Allocates a single 4KB page.

**Returns**:
- Pointer to page-aligned memory (success)
- `NULL` (failure - out of memory)

**Guarantees**:
- Returns zeroed memory (memset to 0)
- Page-aligned (address % 4096 == 0)
- Interrupt-safe (uses IRQ save/restore)

**Implementation**: Wrapper around `palloc_alloc_contig(1)`

**Performance**: O(n) worst case - must scan bitmap to find free page

**Example**:
```c
void *page = palloc_alloc();
if (!page) {
    uart_puts("Out of memory!\n");
    return -1;
}
// page is now 4096 bytes of zeroed, page-aligned memory
```

### palloc_alloc_contig()

```c
void *palloc_alloc_contig(size_t count);
```

**Purpose**: Allocates multiple **contiguous** pages.

**Parameters**:
- `count`: Number of consecutive pages needed

**Returns**:
- Pointer to first page (success)
- `NULL` (failure)

**Algorithm** (First-Fit):

```
1. Scan bitmap left-to-right
2. Track consecutive free pages
3. When 'count' consecutive found:
   - Mark all as used
   - Zero the entire region
   - Return pointer to first page
4. If scan completes without finding 'count' consecutive:
   - Return NULL
```

**Complexity**: O(n) where n = total_pages

**Critical Section**: Uses `irq_save()`/`irq_restore()` to prevent race conditions

**Fragmentation Risk**: Can fail even with enough total free pages if they're not contiguous

**Example**:
```c
// Allocate 16KB (4 consecutive pages) for DMA buffer
void *dma_buf = palloc_alloc_contig(4);
if (!dma_buf) {
    uart_puts("Cannot allocate contiguous memory\n");
}
```

**Use Cases**:
- Large kernel data structures
- DMA buffers (require physical contiguity)
- Page tables (MMU requires aligned page table memory)
- kmalloc heap expansion

### palloc_free()

```c
void palloc_free(void *ptr, size_t count);
```

**Purpose**: Frees one or more pages starting at `ptr`.

**Parameters**:
- `ptr`: Pointer to first page (must be page-aligned)
- `count`: Number of consecutive pages to free

**Validation**:
```c
if (!ptr || count == 0) return;                        // Null check
if (offset % PAGE_SIZE != 0) return;                   // Alignment check
if (idx + count > total_pages) return;                 // Bounds check
```

**Safety Features**:
- Ignores invalid pointers (non-aligned, out of range)
- Interrupt-safe
- No double-free detection (marks as free regardless)

**Example**:
```c
void *pages = palloc_alloc_contig(8);  // 32KB
// ... use memory ...
palloc_free(pages, 8);  // Free all 8 pages
```

### palloc_free_one()

```c
static inline void palloc_free_one(void *ptr);
```

**Purpose**: Helper function to free a single page.

**Implementation**: `palloc_free(ptr, 1)`

**Use Case**: Convenience wrapper for common single-page frees

### palloc_get_free_pages()

```c
size_t palloc_get_free_pages(void);
```

**Purpose**: Returns count of free pages.

**Returns**: Number of unallocated pages

**Complexity**: O(n) - scans entire bitmap

**Use Cases**:
- Memory monitoring (ps, free commands)
- OOM detection
- Debug logging

**Note**: Not real-time safe (slow for large pools)

## Implementation Details

### Bitmap Operations

#### is_free(idx)

```c
static int is_free(size_t idx) {
    return !(page_bitmap[idx / 8] & (1 << (idx % 8)));
}
```

**Explanation**:
- `idx / 8`: Which byte contains the bit
- `idx % 8`: Which bit within that byte (0-7)
- `&`: Test if bit is set
- `!`: Invert (0=used becomes false, 1=free becomes true)

**Example**: Check if page 19 is free:
- Byte: 19/8 = 2
- Bit: 19%8 = 3
- Check: `page_bitmap[2] & (1 << 3)` → tests bit 3 of byte 2

#### mark_used(idx)

```c
static void mark_used(size_t idx) {
    page_bitmap[idx / 8] |= (1 << (idx % 8));
}
```

**Effect**: Sets bit to 1 (marks page as allocated)

**Bitwise OR**: `|=` preserves other bits in the byte

#### mark_free(idx)

```c
static void mark_free(size_t idx) {
    page_bitmap[idx / 8] &= ~(1 << (idx % 8));
}
```

**Effect**: Clears bit to 0 (marks page as free)

**Bitwise AND with NOT**: Clears target bit while preserving others

### Thread Safety

All allocation/free operations use:

```c
unsigned long flags = irq_save();
// ... critical section ...
irq_restore(flags);
```

**Why**: Prevents race conditions between:
- Multiple tasks allocating simultaneously
- Interrupt handlers allocating memory
- Scheduler reallocating pages

**Performance Impact**: Minimal - allocations are fast, IRQs disabled briefly

### Memory Zeroing

```c
memset(ptr, 0, count * PAGE_SIZE);
```

**Why Zero Memory?**
1. **Security**: Prevents information leakage between processes
2. **Determinism**: Uninitialized memory has unpredictable values
3. **Bug Detection**: Makes use-before-init bugs more reproducible

**Performance**: ~500 cycles per page on AArch64 (negligible for 4KB)

## Design Decisions

### Why Bitmap?

**Alternatives Considered**:
- **Free List**: More complex, fragmentation issues
- **Buddy Allocator**: Good for power-of-2 sizes, overkill for fixed 4KB pages
- **Bitmap**: Simple, fast, minimal overhead ✓

**Advantages**:
- O(1) mark/unmark operations
- Easy to scan for contiguous regions
- Minimal memory overhead (~0.003%)
- No fragmentation of metadata

**Disadvantages**:
- O(n) allocation time (must scan)
- No fast way to find "best fit"

### Why 4KB Pages?

1. **AArch64 Standard**: MMU uses 4KB page tables
2. **Cache Friendly**: Matches L1/L2 cache line multiples
3. **TLB Efficient**: One TLB entry covers 4KB
4. **Industry Standard**: Linux, BSD, Windows use 4KB on ARM64

### Why First-Fit?

**Algorithm**: Allocate from first available contiguous block

**Alternatives**:
- **Best-Fit**: Minimize fragmentation but O(n) to find smallest sufficient block
- **Next-Fit**: Remember last position, but causes fragmentation
- **Buddy System**: Complex, better for variable sizes

**Chosen**: First-fit for simplicity; sufficient for kernel allocator

### Why No Defragmentation?

**Physical Memory**: Cannot move pages without:
- Updating all page table references
- Flushing TLBs
- Potential kernel data structure corruption

**Decision**: Accept fragmentation; use contiguous allocation carefully

## Integration with Other Subsystems

### MMU (mmu.c)

```c
// mmu_init() allocates page tables
uint64_t *kernel_l0 = (uint64_t *)palloc_alloc();
```

**Requirement**: Page tables must be page-aligned (palloc guarantees this)

### kmalloc (kmalloc.c)

```c
// kmalloc expands heap by allocating pages
void *page = palloc_alloc();
```

**Layering**: kmalloc builds variable-size allocator on top of palloc

### Task Stack Allocation (sched.c)

```c
// Each task gets 16KB stack (4 pages)
task->stack = palloc_alloc_contig(4);
```

**Requirement**: Stack must be contiguous for efficient access

### DMA Buffers (virtio.c)

```c
// VirtIO needs physically contiguous buffers
void *dma_buf = palloc_alloc_contig(buffer_pages);
```

**Hardware Requirement**: DMA controllers need contiguous physical addresses

## Memory Layout Example

```
Total RAM: 256MB
Pool Start: 0x50000000
Total Pages: 65,536 (256MB / 4KB)

After some allocations:
┌───────────────────────────────────────┐ 0x50000000
│ Page 0-3: MMU L0/L1/L2/L3 tables      │ USED (4 pages)
├───────────────────────────────────────┤ 0x50004000
│ Page 4-7: Task 1 stack                │ USED (4 pages)
├───────────────────────────────────────┤ 0x50008000
│ Page 8-11: kmalloc heap               │ USED (4 pages)
├───────────────────────────────────────┤ 0x5000C000
│ Page 12-15: FREE                      │ FREE
├───────────────────────────────────────┤ 0x50010000
│ Page 16-31: DMA buffer                │ USED (16 pages)
├───────────────────────────────────────┤ 0x50020000
│ ... (remaining 65,504 pages) ...      │ MIXED
└───────────────────────────────────────┘ 0x60000000

Bitmap (first 4 bytes):
Byte 0: 11111111 (pages 0-7 used)
Byte 1: 11111111 (pages 8-15 used)
Byte 2: 11111111 (pages 16-23 used)
Byte 3: 11110000 (pages 24-27 used, 28-31 free)
```

## Error Handling

### Out of Memory

```c
uart_puts("[palloc] CRITICAL: OUT OF MEMORY (CONTIG)! requested=...);
return NULL;
```

**When**: No contiguous block of requested size available

**Recovery**: Caller must handle NULL return (no automatic recovery)

**System State**: Kernel may panic if critical allocation fails (e.g., page table allocation)

### Invalid Parameters

```c
if (count == 0 || count > total_pages) {
    uart_puts("[palloc] ERROR: invalid count=...);
    return NULL;
}
```

**Prevention**: Validates all inputs before allocation attempt

### Misaligned Free

```c
if (offset % PAGE_SIZE != 0) {
    uart_puts("[palloc] free invalid align\n");
    return;
}
```

**Safety**: Silently ignores (prevents corrupting bitmap)

## Performance Characteristics

### palloc_alloc()
- **Best Case**: O(1) - first page is free
- **Average**: O(n/2) - halfway through bitmap
- **Worst Case**: O(n) - last page is free or none free

### palloc_alloc_contig(k)
- **Best Case**: O(k) - first k pages are free
- **Worst Case**: O(n) - scan entire bitmap

### palloc_free()
- **Time**: O(1) - direct bitmap manipulation
- **No coalescing**: Freed pages immediately reusable

### palloc_get_free_pages()
- **Time**: O(n) - must scan all bits
- **Use sparingly**: Expensive for monitoring

## Debugging

### Common Issues

**1. Double Allocation**:
```
Symptom: Two components get same page
Cause: Concurrent allocation without IRQ protection
Debug: Add allocation logging
```

**2. Fragmentation**:
```
Symptom: palloc_alloc_contig() fails despite enough total free pages
Cause: Free pages scattered, not contiguous
Debug: Print bitmap to see fragmentation
```

**3. Leak Detection**:
```c
// At shutdown, check for leaks:
size_t leaked = total_pages - palloc_get_free_pages();
uart_puts("Leaked pages: "); uart_put_hex(leaked);
```

### Debug Functions (Add These)

```c
void palloc_dump_bitmap(void) {
    for (size_t i = 0; i < total_pages; i++) {
        if (i % 64 == 0) uart_puts("\n");
        uart_putc(is_free(i) ? '.' : 'X');
    }
}

void palloc_print_largest_free_block(void) {
    size_t max_contig = 0, current_contig = 0;
    for (size_t i = 0; i < total_pages; i++) {
        if (is_free(i)) {
            current_contig++;
            if (current_contig > max_contig)
                max_contig = current_contig;
        } else {
            current_contig = 0;
        }
    }
    uart_puts("Largest free block: "); 
    uart_put_hex(max_contig);
    uart_puts(" pages\n");
}
```

## Security Considerations

### Memory Leakage Prevention

**Current**: Memory is zeroed on allocation
**Attack**: A malicious process could leave sensitive data in freed pages

**Mitigation**: Pages are zeroed before reallocation (implemented)

### Out-of-Bounds Access

**Current**: Bitmap validates index before access
**Attack**: Corrupting `total_pages` could allow OOB writes

**Mitigation**: `total_pages` is static - not exposed to user space

### Denial of Service

**Attack**: Allocate all pages, starve other processes
**Mitigation**: Task limits (not yet implemented)

**Future**: Per-task allocation quotas

## Cross-References

- **kmalloc**: [kmalloc.c](kmalloc.c.md) - Variable-size allocator built on palloc
- **MMU**: [mmu.c](mmu.c.md) - Uses palloc for page table allocation
- **Scheduler**: [03-TASK-SCHEDULING.md](../03-TASK-SCHEDULING.md) - Allocates task stacks
- **Boot**: [kernel.c](../core/kernel.c.md) - Calls `palloc_init()` during startup
- **Interrupts**: [irq.c](../io/irq.c.md) - `irq_save()` used for thread safety

## Future Enhancements

1. **Memory Zones**: Separate DMA-able vs normal memory
2. **NUMA Support**: Multi-node memory (for large systems)
3. **Page Coloring**: Cache-aware allocation
4. **Lazy Zeroing**: Zero pages on first use, not allocation
5. **Memory Pressure**: Callbacks when low memory
6. **Per-CPU Freelists**: Reduce contention on multicore
7. **Huge Pages**: Support 2MB/1GB pages for efficiency
8. **Allocation Tracking**: Per-subsystem accounting

## Historical Notes

The bitmap allocator is a classic design used in many operating systems (Linux, Minix, xv6). It's simple, reliable, and sufficient for systems without extreme memory pressure. More sophisticated allocators (slab, buddy) are typically built on top of a page allocator like this one.
