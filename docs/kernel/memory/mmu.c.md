# MMU & Page Tables - mmu.c/h

## Overview

`mmu.c` implements **Memory Management Unit (MMU) configuration** and **page table management** for AArch64. It handles:
- Virtual-to-physical address translation
- Memory protection (read/write/execute permissions)
- Cache configuration
- User/kernel space separation

**Critical Role**: Enables memory protection, preventing tasks from accessing each other's memory or corrupting the kernel.

## AArch64 MMU Architecture

### Translation Granule

MYRASPOS uses **4KB pages** with **48-bit virtual addressing**:

```
Virtual Address (48 bits):
┌─────┬─────┬─────┬─────┬──────────┐
│ L0  │ L1  │ L2  │ L3  │  Offset  │
│ 9b  │ 9b  │ 9b  │ 9b  │   12b    │
└─────┴─────┴─────┴─────┴──────────┘
  39-47 30-38 21-29 12-20   0-11

L0: 512 entries × 512GB = 256TB
L1: 512 entries × 1GB   = 512GB
L2: 512 entries × 2MB   = 1GB
L3: 512 entries × 4KB   = 2MB
```

### Translation Process

```
VA[47:39] → L0[index] → Points to L1 table
VA[38:30] → L1[index] → Points to L2 table  
VA[29:21] → L2[index] → Points to L3 table
VA[20:12] → L3[index] → Page Table Entry (PTE)
VA[11:0]  → Offset within 4KB page
```

### Address Spaces

```
0x0000_0000_0000_0000 ┌──────────────────┐
                      │  Kernel Space    │ L0[0]: Kernel mappings
                      │  (TTBR0_EL1)     │
0x0000_0080_0000_0000 ├──────────────────┤ 512GB boundary
                      │  User Space      │ L0[1+]: User mappings
                      │  (TTBR0_EL1)     │
0x0001_0000_0000_0000 └──────────────────┘
```

**Design Decision**: Use TTBR0 for all translations (kernel + user), with L0[0] for kernel, L0[1+] for user.

## Data Structures

### Page Table Entry (PTE) Flags

```c
#define PTE_VALID           (1ULL << 0)   // Entry is valid
#define PTE_TABLE           (1ULL << 1)   // Points to next level (L0/L1/L2)
#define PTE_PAGE            (1ULL << 1)   // Page entry (L3)
#define PTE_AF              (1ULL << 10)  // Access Flag (must be 1)
#define PTE_SH_INNER        (3ULL << 8)   // Inner shareable
#define PTE_MEMATTR_NORMAL  (0ULL << 2)   // Normal memory (MAIR index 0)
#define PTE_MEMATTR_DEVICE  (1ULL << 2)   // Device memory (MAIR index 1)
#define PTE_USER            (1ULL << 6)   // User accessible
#define PTE_RDONLY          (1ULL << 7)   // Read-only
#define PTE_PXN             (1ULL << 53)  // Privileged Execute Never
#define PTE_UXN             (1ULL << 54)  // Unprivileged Execute Never
```

### Memory Attributes (MAIR)

```c
#define MAIR_DEVICE_nGnRnE  0x00ULL  // Device: non-Gathering, non-Reordering, no Early ack
#define MAIR_NORMAL_WB      0xFFULL  // Normal: Write-Back, Read/Write Allocate
#define MAIR_VALUE          ((MAIR_NORMAL_WB << 0) | (MAIR_DEVICE_nGnRnE << 8))
```

**MAIR_EL1 Register**:
- Index 0: Normal memory (cacheable, write-back)
- Index 1: Device memory (non-cacheable, strict ordering)

### Translation Control (TCR)

```c
#define TCR_T0SZ(n)         ((64ULL - (n)) << 0)  // Virtual address size
#define TCR_IRGN0_WBWA      (1ULL << 8)           // Inner cache: Write-Back Write-Allocate
#define TCR_ORGN0_WBWA      (1ULL << 10)          // Outer cache: Write-Back Write-Allocate
#define TCR_SH0_INNER       (3ULL << 12)          // Inner shareable
#define TCR_TG0_4KB         (0ULL << 14)          // 4KB granule
```

**TCR_VALUE**: Configures 48-bit VA space with write-back caching

## API Reference

### mmu_init()

```c
void mmu_init(void);
```

**Purpose**: Initializes MMU with kernel mappings and enables address translation.

**Sequence**:
1. Allocate L0 page table
2. Map peripherals (0x0 - 0x40000000) as DEVICE memory
3. Map RAM (0x40000000 - 0x60000000) as NORMAL memory
4. Configure MAIR_EL1, TCR_EL1, TTBR0_EL1
5. Enable MMU by setting SCTLR_EL1.M bit
6. Enable I-cache and D-cache

**Memory Map Created**:
```
0x00000000 - 0x40000000 (1GB):   Peripherals (DEVICE, R/W)
  ├─ 0x09000000: UART
  ├─ 0x08000000: GIC
  └─ 0x0a000000: VirtIO
0x40000000 - 0x60000000 (512MB): RAM (NORMAL, R/W/X)
  ├─ Kernel code/data
  ├─ Framebuffer
  └─ Page allocator pool
```

**Called from**: `kernel_main()` before any MMU-dependent operations

**Effect**: After this, all memory accesses are translated through page tables

### mmu_map()

```c
void mmu_map(uintptr_t va, uintptr_t pa, size_t size, uint64_t flags);
```

**Purpose**: Maps a virtual address range to physical addresses in the **kernel** page table.

**Parameters**:
- `va`: Virtual address (start)
- `pa`: Physical address (start)
- `size`: Size in bytes (rounded up to 4KB)
- `flags`: PTE flags (permissions, memory type)

**Example**:
```c
// Map framebuffer as device memory
mmu_map(0x40000000, 0x40000000, 4*1024*1024, 
        PTE_AF | PTE_MEMATTR_DEVICE);

// Map kernel code as normal memory
mmu_map(0x40800000, 0x40800000, 2*1024*1024,
        PTE_AF | PTE_SH_INNER | PTE_MEMATTR_NORMAL);
```

**Implementation**: Calls `mmu_map_table()` with kernel PGD

### mmu_map_table()

```c
int mmu_map_table(uint64_t *pgd, uintptr_t va, uintptr_t pa, size_t size, uint64_t flags);
```

**Purpose**: Maps VA→PA in a **specific** page table (kernel or user).

**Parameters**:
- `pgd`: Page Global Directory (L0 table)
- `va`, `pa`, `size`: Address range
- `flags`: PTE flags

**Returns**:
- `0`: Success
- `-1`: Failure (out of memory for page tables)

**Algorithm**:
```
For each 4KB page in [va, va+size):
  1. Calculate L0/L1/L2/L3 indices from VA
  2. Walk page table hierarchy, allocating missing levels
  3. Set L3[index] = PA | flags | PTE_VALID | PTE_PAGE
  4. Flush cache lines (dc civac)
  5. Memory barrier (dmb sy)
  6. Increment PA by 4KB
```

**Cache Management**: Uses `dc civac` to flush page table updates to RAM (critical for MMU coherency)

### get_next_level()

```c
static uint64_t *get_next_level(uint64_t *current_table, int index, int alloc);
```

**Purpose**: Traverses or creates page table levels.

**Parameters**:
- `current_table`: Current level table
- `index`: Entry index (0-511)
- `alloc`: 1=allocate if missing, 0=return NULL if missing

**Returns**:
- Pointer to next level table
- `NULL` if not present and `alloc==0`

**Behavior**:
```c
if (!(current_table[index] & PTE_VALID)) {
    if (!alloc) return NULL;
    uint64_t *next = palloc_alloc();  // Allocate 4KB table
    memset(next, 0, PAGE_SIZE);       // Zero entries
    // Flush to RAM
    current_table[index] = (uintptr_t)next | PTE_TABLE | PTE_VALID;
    return next;
}
return (uint64_t *)(current_table[index] & ~0xFFFULL);  // Mask off flags
```

**Critical**: Flushes cache after allocating/modifying page tables

### mmu_create_user_pgd()

```c
uint64_t* mmu_create_user_pgd(void);
```

**Purpose**: Creates a new page table for user space.

**Returns**:
- Pointer to new L0 table
- `NULL` (out of memory)

**Behavior**:
```c
uint64_t *pgd = palloc_alloc();
memset(pgd, 0, PAGE_SIZE);
pgd[0] = kernel_l0[0];  // Share kernel mappings (L0 index 0)
// L0 indices 1-511 are for user mappings
return pgd;
```

**Sharing Kernel Mappings**: Copies L0[0] entry (points to kernel L1 table), so user tasks can make syscalls

**Address Space Layout**:
```
User PGD:
L0[0]: → Kernel L1 (shared)    | 0x0000_0000_0000_0000 - 0x0000_007F_FFFF_FFFF
L0[1]: → User L1 (private)      | 0x0000_0080_0000_0000 - 0x0000_00FF_FFFF_FFFF
...
```

### mmu_free_user_pgd()

```c
void mmu_free_user_pgd(uint64_t *pgd);
```

**Purpose**: Frees a user page table.

**Parameters**:
- `pgd`: Page table to free (from `mmu_create_user_pgd()`)

**Behavior**:
```c
if (!pgd || pgd == kernel_l0) return;  // Safety checks
palloc_free(pgd, 1);  // Free L0 page
```

**TODO**: Should recursively free L1/L2/L3 tables (currently leaks)

**Safety**: Never frees kernel page table

### mmu_map_page()

```c
int mmu_map_page(uint64_t *pgd, uintptr_t va, uintptr_t pa, uint64_t flags);
```

**Purpose**: Maps a single 4KB page.

**Implementation**: `return mmu_map_table(pgd, va, pa, PAGE_SIZE, flags);`

**Use Case**: Fine-grained control (e.g., mapping individual user pages)

### mmu_switch()

```c
void mmu_switch(uint64_t *pgd);
```

**Purpose**: Switches active page table (for context switching).

**Parameters**:
- `pgd`: New page table (or NULL for kernel-only)

**Behavior**:
```assembly
msr ttbr0_el1, <pgd>     // Load new page table base
isb                       // Instruction barrier
tlbi vmalle1is           // Invalidate all TLBs
dsb sy                    // Data barrier
isb                       // Instruction barrier
```

**TLB Invalidation**: Flushes Translation Lookaside Buffer to prevent stale translations

**Performance Impact**: ~100 cycles (expensive, but necessary for security)

**Called from**: `task_switch()` in scheduler when changing tasks

### mmu_get_kernel_pgd()

```c
uint64_t* mmu_get_kernel_pgd(void);
```

**Purpose**: Returns pointer to kernel page table.

**Use Case**: Scheduler needs kernel PGD for kernel-only tasks

## Implementation Details

### Bootstrap Problem

**Challenge**: Page tables themselves must be in memory before MMU is enabled.

**Solution**:
1. Allocate L0 table using `palloc_alloc()` (works without MMU)
2. Build page tables in physical memory
3. Use identity mapping (VA == PA) for kernel
4. Enable MMU
5. Now all accesses are translated

### Cache Coherency

**Problem**: Page table updates must be visible to MMU hardware.

**Solution**: `dc civac` (Data Cache Clean and Invalidate by VA to PoC)

```c
__asm__ volatile("dc civac, %0" : : "r" (address) : "memory");
__asm__ volatile("dmb sy" ::: "memory");
```

**Why Necessary**:
- CPU writes page table entries to cache
- MMU reads page tables directly from RAM
- Without flush, MMU sees stale data

**Point of Coherency (PoC)**: Level where all observers see same data

### Memory Barriers

```c
__asm__ volatile("dsb sy");  // Data Synchronization Barrier
__asm__ volatile("isb");     // Instruction Synchronization Barrier
```

**DSB**: Ensures all prior memory accesses complete before continuing

**ISB**: Flushes instruction pipeline, ensures subsequent instructions see updates

**Use Cases**:
- After modifying system registers (TTBR0, SCTLR)
- After TLB invalidation
- After cache maintenance operations

### TLB Management

**Translation Lookaside Buffer**: Caches VA→PA translations for speed

**Invalidation Required**:
- When page tables are modified
- When switching between tasks
- When unmapping pages

```c
__asm__ volatile("tlbi vmalle1is");  // Invalidate all, Inner Shareable
```

**Performance**: TLB misses are expensive (~100 cycles), so invalidate sparingly

### Page Table Walk Example

```
Map VA 0x0000_0080_1234_5678 → PA 0x5000_0000:

1. L0 index = (0x80_1234_5678 >> 39) & 0x1FF = 1
   kernel_l0[1] = <allocate L1 table>

2. L1 index = (0x80_1234_5678 >> 30) & 0x1FF = 0
   L1[0] = <allocate L2 table>

3. L2 index = (0x80_1234_5678 >> 21) & 0x1FF = 145
   L2[145] = <allocate L3 table>

4. L3 index = (0x80_1234_5678 >> 12) & 0x1FF = 69
   L3[69] = 0x5000_0000 | flags | PTE_VALID | PTE_PAGE
```

### BSS Invalidation

```c
for (uintptr_t addr = bss_start_addr; addr < bss_end_addr; addr += 64) {
    __asm__ volatile("dc civac, %0" : : "r" (addr) : "memory");
}
```

**Why**: BSS was zeroed before MMU enabled; ensure zeros are in RAM, not just cache

## Memory Protection Scenarios

### Kernel Code (R/X)

```c
mmu_map(kernel_code_start, kernel_code_start, code_size,
        PTE_AF | PTE_SH_INNER | PTE_MEMATTR_NORMAL);  // No PTE_RDONLY
```

**Effect**: Readable, executable, NOT writable (W^X security)

### Kernel Data (R/W)

```c
mmu_map(kernel_data_start, kernel_data_start, data_size,
        PTE_AF | PTE_SH_INNER | PTE_MEMATTR_NORMAL | PTE_PXN);
```

**Effect**: Readable, writable, NOT executable

### User Code (User R/X)

```c
mmu_map_page(user_pgd, user_code_va, user_code_pa,
             PTE_AF | PTE_MEMATTR_NORMAL | PTE_USER);
```

**Effect**: User can read/execute, kernel can read/write/execute

### MMIO Registers (Device R/W)

```c
mmu_map(UART_BASE, UART_BASE, PAGE_SIZE,
        PTE_AF | PTE_MEMATTR_DEVICE | PTE_PXN | PTE_UXN);
```

**Effect**: Readable, writable, NOT executable, strict ordering

## Design Decisions

### Why 48-bit VA?

**AArch64 supports**: 36-bit, 39-bit, 42-bit, 48-bit

**Chosen 48-bit**:
- 256TB address space (generous)
- Standard Linux configuration
- 4-level page tables (reasonable depth)

**Trade-off**: More page table memory vs. larger address space

### Why TTBR0 for Everything?

**Alternative**: TTBR0 for user, TTBR1 for kernel (like Linux)

**Chosen single TTBR0**:
- Simpler implementation
- Fewer register updates on syscalls
- Kernel and user share page table hierarchy

**Advantage**: Syscalls don't require page table switch

### Why Share Kernel Mappings?

**Sharing L0[0]**: All user tasks point to same kernel L1 table

**Benefits**:
- Syscalls don't need page table switch
- Kernel code always accessible
- Single copy of kernel page tables

**Security**: Kernel mappings are PTE_USER=0, so user can't access

### Why Identity Mapping?

**Kernel uses VA == PA**:
- Simplifies debugging (addresses match)
- No need for complex relocation
- Boot code easier (already at correct address)

**Trade-off**: Less flexibility, but MYRASPOS doesn't need complex layouts

## Performance Characteristics

### Page Table Walk

**TLB Hit**: ~1 cycle (cached translation)

**TLB Miss**: ~100 cycles:
- 4 memory accesses (L0→L1→L2→L3)
- Each ~20-30 cycles depending on cache

**Optimization**: Keep TLB hot by avoiding excessive invalidations

### mmu_map_table()

**Time per page**: ~200 cycles
- Page table walk: 4 memory accesses
- Allocate missing tables: palloc_alloc() if needed
- Cache flushes: 10+ cycles

**For 256MB mapping**: ~13 million cycles (~10ms at 1GHz)

### mmu_switch()

**Cost**: ~100 cycles
- Register write: ~10 cycles
- TLB invalidation: ~50 cycles
- Barriers: ~30 cycles

**Context switch overhead**: Significant (5-10% of total switch time)

## Cross-References

- **palloc**: [palloc.c](palloc.c.md) - Allocates page table memory
- **Boot**: [start.s](../../boot/start.s.md) - Sets up initial identity mapping
- **Scheduler**: [03-TASK-SCHEDULING.md](../03-TASK-SCHEDULING.md) - Calls `mmu_switch()`
- **Syscalls**: [syscall.c](../services/syscall.c.md) - Relies on shared kernel mappings

## Security Considerations

### W^X (Write XOR Execute)

**Enforcement**: No region should be both writable and executable

**Current State**: Partially enforced (kernel code is R/X, data is R/W)

**TODO**: Use PTE_PXN/PTE_UXN consistently

### User/Kernel Separation

**PTE_USER bit**: Controls whether user code can access

**Kernel mappings**: PTE_USER=0 (inaccessible from EL0)

**User mappings**: PTE_USER=1 (accessible from EL0 and EL1)

### KASLR (Kernel Address Space Layout Randomization)

**Not implemented**: Kernel loads at fixed address

**Security impact**: Attackers know kernel addresses

**Future**: Randomize kernel load address

## Debugging

### Common Issues

**1. Page Fault on Access**:
```
Symptom: Data Abort exception
Cause: Accessing unmapped VA
Debug: Check page tables with JTAG debugger
```

**2. TLB Stale Entry**:
```
Symptom: Old mapping still active after unmap
Cause: Missing TLB invalidation
Debug: Add tlbi after page table modification
```

**3. Cache Incoherency**:
```
Symptom: MMU sees old page table entries
Cause: Missing dc civac after write
Debug: Add cache maintenance ops
```

### Inspection

```c
void mmu_dump_l0(uint64_t *pgd) {
    for (int i = 0; i < 512; i++) {
        if (pgd[i] & PTE_VALID) {
            uart_puts("L0["); uart_put_hex(i); uart_puts("] = ");
            uart_put_hex(pgd[i]); uart_puts("\n");
        }
    }
}
```

## Future Enhancements

1. **Demand Paging**: Allocate pages on first access
2. **Copy-on-Write**: Share pages between tasks until modified
3. **Huge Pages**: Use 2MB/1GB pages for large mappings
4. **SMMU Support**: System MMU for device DMA protection
5. **Page Table Compression**: Reduce memory overhead
6. **ASID**: Address Space ID to avoid TLB flush on switch
7. **Recursive Page Tables**: Map page tables into VA space
8. **Guard Pages**: Unmapped pages around stacks

## Historical Notes

AArch64's 4-level page tables are inherited from x86-64. The 48-bit VA space is a sweet spot between address space size and page table overhead. Modern OSes (Linux, BSD) use similar configurations, making MYRASPOS's design compatible with standard toolchains and debugging tools.
