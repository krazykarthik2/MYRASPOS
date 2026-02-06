# Linker Script - linker.ld (QEMU)

## Overview

`linker.ld` is the **QEMU linker script** that defines the memory layout of the MYRASPOS kernel for the QEMU `virt` machine. It specifies where code, data, BSS, and stack are located in memory and how they're organized in the final executable.

## Key Differences from linker_pi.ld

This linker script is specifically for QEMU and differs from the Raspberry Pi version in:
- **Load Address**: 0x40800000 (high memory) vs 0x80000 (low memory)
- **Program Headers**: Includes ELF program headers for proper segment permissions
- **Alignment**: Stricter alignment requirements for MMU page boundaries

## Memory Layout

```
0x40000000 ┌─────────────────┐
           │  Framebuffer    │  (~4MB for 1024x768x4)
           │  (GPU MMIO)     │
0x40800000 ├─────────────────┤ ← Kernel Load Address
           │  .text.boot     │  Boot code (start.s)
           │  .text          │  Executable code
           │  .rodata        │  Read-only data (strings, constants)
           ├─────────────────┤ (4KB aligned)
           │  .data          │  Initialized global variables
           ├─────────────────┤ (4KB aligned)
           │  .bss           │  Uninitialized globals (zeroed at boot)
           ├─────────────────┤
           │  (padding)      │
           │  Stack (64KB)   │  Grows downward
           │  stack_top      │ ← Initial SP
           └─────────────────┘
```

## Section-by-Section Analysis

### ENTRY(_start) (Line 1)

```ld
ENTRY(_start)
```

**Purpose**: Declares `_start` as the entry point of the executable.

**Effect**: 
- Bootloader/QEMU jumps to this symbol after loading
- Corresponds to `_start` in `boot/start.s`
- Required for ELF executables to know where to begin execution

### PHDRS (Lines 3-7)

```ld
PHDRS
{
  text PT_LOAD FLAGS(5);  /* R + X */
  data PT_LOAD FLAGS(6);  /* R + W */
}
```

**Purpose**: Defines ELF program headers with specific permissions.

**Flags**:
- `5 = R + X` (Read + Execute): For code and read-only data
- `6 = R + W` (Read + Write): For mutable data

**Why**: 
- Enables proper MMU page table permissions
- Separates executable code from writable data (W^X security principle)
- QEMU's ELF loader respects these flags

**Cross-reference**: See [mmu.c](../kernel/memory/mmu.c.md) for how the MMU enforces these permissions.

### Load Address (Lines 12-13)

```ld
/* Move kernel base further up to avoid collision with framebuffer at 0x40000000.
   A 1024x768x4 framebuffer takes ~4MB. 0x40800000 is 8MB offset. */
. = 0x40800000;
```

**Purpose**: Sets the kernel base address to 0x40800000.

**Design Decision - Why 0x40800000?**

1. **Framebuffer Avoidance**: 
   - QEMU's virtio-gpu maps framebuffer at 0x40000000
   - 1024×768×4 bytes = 3,145,728 bytes (~3MB)
   - 8MB offset provides safety margin for larger resolutions

2. **QEMU virt Machine Layout**:
   ```
   0x00000000 - 0x08000000: Flash memory
   0x08000000 - 0x08020000: GIC (interrupt controller)
   0x09000000 - 0x09001000: UART
   0x0a000000 - 0x0a000200: RTC
   0x40000000 - 0x48000000: RAM (DRAM typically starts here)
   ```

3. **RAM Availability**: QEMU virt provides 128MB+ RAM starting at 0x40000000

**Constraint**: Must match the load address expected by QEMU and specified in boot command.

### .text Section (Lines 15-18)

```ld
.text : ALIGN(4) {
  KEEP(*(.text.boot))
  *(.text .text.*)
} :text
```

**Purpose**: Contains all executable code.

**Key Points**:
- **KEEP(.text.boot)**: Ensures boot code is never discarded by linker optimization
- **Order**: `.text.boot` comes first, so `_start` is at the beginning
- **ALIGN(4)**: 4-byte alignment (AArch64 instructions are 4 bytes)
- `:text`: Assigned to the `text` program header (R+X permissions)

**Symbols Included**: All functions compiled from C and assembly

### .rodata Section (Lines 20-22)

```ld
.rodata : ALIGN(4) {
  *(.rodata .rodata.*)
} :text
```

**Purpose**: Contains read-only data (string literals, const arrays).

**Key Points**:
- Also assigned to `:text` program header (read-only + executable)
- **Design Decision**: Grouped with code for cache efficiency and security
- Cannot be modified at runtime (enforced by MMU)

**Examples**: 
- String literals: `"Hello, World!"`
- Const arrays: `const int table[] = {1,2,3};`
- Jump tables for switch statements

### .data Section (Lines 24-26)

```ld
.data : ALIGN(4096) {
  *(.data .data.*)
} :data
```

**Purpose**: Contains initialized global and static variables.

**Key Points**:
- **ALIGN(4096)**: 4KB page boundary alignment for MMU
- `:data` program header (R+W permissions)
- Stored in executable (increases file size)

**Examples**:
```c
int global_counter = 42;
static char buffer[256] = "initialized";
```

**Why 4KB Alignment?**: 
- AArch64 uses 4KB pages for memory management
- Allows separate page table entries for code vs. data
- Enables W^X (write XOR execute) protection

### .bss Section (Lines 28-33)

```ld
.bss : ALIGN(4096) {
  __bss_start = .;
  *(.bss .bss.*)
  *(COMMON)
  __bss_end = .;
} :data
```

**Purpose**: Uninitialized data segment (zeroed by `start.s`).

**Key Points**:
- **__bss_start, __bss_end**: Symbols used by boot code to zero the region
- **COMMON**: Legacy section for tentative definitions
- Not stored in executable (saves file space)
- Must be zeroed at runtime (C standard requirement)

**Examples**:
```c
int uninitialized_global;
static char large_buffer[4096];
```

**Memory Efficiency**: A 1MB uninitialized array adds 0 bytes to the executable but 1MB to runtime memory.

### Stack Setup (Lines 35-37)

```ld
. = ALIGN(8);
. = . + 0x10000;
stack_top = .;
```

**Purpose**: Allocates 64KB (0x10000 bytes) for the initial kernel stack.

**Key Points**:
- **ALIGN(8)**: Ensures 8-byte alignment (AArch64 SP must be 16-byte aligned, but linker uses 8)
- **0x10000**: 65,536 bytes (64KB)
- **stack_top**: Symbol used by `start.s` to initialize SP

**Stack Characteristics**:
- Grows **downward** (from high to low addresses)
- Used by all kernel code during initialization
- Shared by interrupt handlers initially
- Later, tasks get their own stacks (see [sched.c](../kernel/core/03-TASK-SCHEDULING.md))

**Why 64KB?**:
- Boot code uses minimal stack
- Kernel initialization requires moderate stack space
- Sufficient for nested function calls during init
- Smaller than task stacks (which are dynamically allocated)

## Address Calculation Example

For a kernel with:
- Code: 200KB
- Read-only data: 50KB
- Initialized data: 10KB
- BSS: 100KB

```
0x40800000: _start (boot code)
0x40832000: End of .text (~200KB)
0x4083E800: End of .rodata (~50KB)
0x40841000: .data start (4KB aligned)
0x40843800: .bss start (4KB aligned after 10KB data)
0x4085C800: BSS end (~100KB)
0x4086C800: stack_top (64KB after BSS)
```

## Comparison with linker_pi.ld

| Feature | linker.ld (QEMU) | linker_pi.ld (Pi) |
|---------|------------------|-------------------|
| Load Address | 0x40800000 | 0x80000 |
| Program Headers | Yes (PHDRS) | No |
| .data Alignment | 4096 bytes (4KB) | 4 bytes |
| .bss Alignment | 4096 bytes (4KB) | 4 bytes |
| Stack Size | 64KB (0x10000) | 16KB (0x4000) |
| MMU Support | Page-aligned | Basic |

**Why Different?**:
- **QEMU**: Full MMU support, ELF loader respects program headers
- **Pi**: Simpler bootloader, less strict alignment requirements
- **Address Space**: QEMU uses high memory, Pi uses low memory (GPU uses high)

## Symbol Exports

The linker script exports these symbols to C code:

```c
extern char __bss_start[];
extern char __bss_end[];
extern char stack_top[];
```

**Usage in start.s**:
```assembly
ldr x0, =__bss_start
ldr x1, =__bss_end
ldr x0, =stack_top
```

## Design Decisions

### Why Separate Program Headers?

1. **Security**: W^X protection prevents code injection
2. **MMU Efficiency**: Code and data in separate page tables
3. **Cache Optimization**: I-cache for code, D-cache for data

### Why 4KB Alignment?

1. **MMU Pages**: AArch64 uses 4KB page granularity
2. **Page Tables**: Each entry maps 4KB
3. **Protection**: Can't set permissions finer than page boundaries

### Why Large Stack?

1. **Deep Call Stacks**: Kernel initialization has many nested calls
2. **Interrupt Safety**: Interrupts use the same stack initially
3. **Debug Builds**: More stack space for debug info

## Build Process Integration

1. **Compilation**: Source files → object files (.o)
2. **Linking**: `ld -T linker.ld` combines objects using this script
3. **Output**: `kernel8.elf` with defined memory layout
4. **Loading**: QEMU loads ELF, jumps to `_start`

**Build Command** (from build.bat):
```bash
aarch64-none-elf-ld -T linkers/linker.ld -o kernel8.elf $(OBJECTS)
```

## Cross-References

- **Boot Code**: [start.s](start.s.md) - Uses `stack_top`, `__bss_start`, `__bss_end`
- **Memory Management**: [mmu.c](../kernel/memory/mmu.c.md) - Maps regions defined here
- **Memory Allocators**: [palloc.c](../kernel/memory/palloc.c.md), [kmalloc.c](../kernel/memory/kmalloc.c.md)
- **Build System**: [BUILD-SYSTEM.md](../build/BUILD-SYSTEM.md)

## Debugging

### Common Issues

1. **Kernel Doesn't Boot**:
   - Check load address matches QEMU config
   - Verify `_start` is at 0x40800000: `objdump -d kernel8.elf | head`

2. **Framebuffer Corruption**:
   - Kernel might overlap framebuffer
   - Increase load address offset

3. **Stack Overflow**:
   - Increase stack size: `. = . + 0x20000;` (128KB)
   - Add stack canaries in debug builds

4. **BSS Not Zeroed**:
   - Check `__bss_start` < `__bss_end`
   - Verify `start.s` BSS zeroing loop

### Inspection Commands

```bash
# View sections and addresses
aarch64-none-elf-objdump -h kernel8.elf

# View symbol table
aarch64-none-elf-nm kernel8.elf | grep -E "bss|stack"

# View linker map
aarch64-none-elf-ld -T linker.ld -o kernel8.elf $(OBJECTS) -Map=kernel.map
```

## Security Considerations

1. **W^X Protection**: No region is both writable and executable
2. **Stack DEP**: Stack is non-executable (if MMU enabled with NX)
3. **ASLR**: Not implemented (fixed load address)
4. **Separate Sections**: Code and data separation prevents some exploits

## Performance Implications

1. **4KB Alignment**: May waste up to 4KB between sections
2. **Cache Lines**: Alignment helps cache efficiency
3. **TLB Entries**: Page-aligned sections optimize TLB usage

## Future Enhancements

- **KASLR**: Randomize kernel load address for security
- **Separate IRQ Stack**: Dedicated interrupt stack to prevent overflow
- **Guard Pages**: Unmapped pages around stack to catch overflow
- **Multiple Stacks**: Per-CPU stacks for multi-core support
