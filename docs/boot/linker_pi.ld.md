# Linker Script - linker_pi.ld (Raspberry Pi)

## Overview

`linker_pi.ld` is the **Raspberry Pi linker script** that defines the memory layout for MYRASPOS when running on physical Raspberry Pi hardware. It's simpler than the QEMU version and uses the standard Raspberry Pi kernel load address.

## Key Differences from linker.ld (QEMU)

| Feature | linker_pi.ld | linker.ld (QEMU) |
|---------|--------------|------------------|
| Load Address | 0x80000 | 0x40800000 |
| Program Headers | None | PHDRS with R+X/R+W |
| Alignment | 4-byte basic | 4KB page-aligned |
| Stack Size | 16KB | 64KB |
| Target | Physical Pi hardware | QEMU virt machine |
| Complexity | Minimal | Full MMU support |

## Memory Layout

```
0x00000000 ┌─────────────────┐
           │  (Reserved)     │
0x00080000 ├─────────────────┤ ← Kernel Load Address (0x80000)
           │  .text.boot     │  Boot code (start.s)
           │  .text          │  Executable code
           ├─────────────────┤
           │  .rodata        │  Read-only data
           ├─────────────────┤
           │  .data          │  Initialized variables
           ├─────────────────┤
           │  .bss           │  Uninitialized variables
           ├─────────────────┤
           │  Stack (16KB)   │  Initial kernel stack
           │  stack_top      │ ← SP initialized here
           └─────────────────┘
```

## Section-by-Section Analysis

### ENTRY(_start) (Line 1)

```ld
ENTRY(_start)
```

**Purpose**: Declares the entry point for the Raspberry Pi bootloader.

**Raspberry Pi Boot Sequence**:
1. GPU loads `bootcode.bin`, `start.elf` from SD card
2. `start.elf` loads `kernel8.img` to address 0x80000
3. CPU jumps to 0x80000 (the `_start` symbol)

### Load Address (Line 5)

```ld
. = 0x80000;
```

**Purpose**: Sets kernel base to 0x80000 (512KB).

**Why 0x80000?**

1. **Standard Pi Address**: All Raspberry Pi 3/4 kernels load here
2. **Below 1MB**: Keeps kernel in first 1MB of RAM
3. **GPU Reserved Space**: 0x0-0x80000 reserved for GPU firmware and bootloader data

**Raspberry Pi Memory Map**:
```
0x00000000 - 0x00001000: Interrupt vectors (remapped)
0x00001000 - 0x00008000: GPU firmware workspace
0x00008000 - 0x00080000: ATAGS/devicetree blob
0x00080000 - ...........: Kernel image (loaded here)
```

**Constraint**: Cannot use addresses below 0x80000 as they may contain boot firmware data.

### .text Section (Lines 7-11)

```ld
.text : ALIGN(4)
{
    KEEP(*(.text.boot))
    *(.text*)
}
```

**Purpose**: Contains all executable code.

**Key Differences from QEMU Version**:
- **No Program Header**: Pi bootloader doesn't use ELF program headers
- **Wildcard**: `*(.text*)` matches `.text`, `.text.main`, etc.
- **Simpler**: No explicit separation of subsections

**KEEP(.text.boot)**: Prevents linker from discarding boot code even if seemingly unused.

### .rodata Section (Lines 13-16)

```ld
.rodata : ALIGN(4)
{
    *(.rodata*)
}
```

**Purpose**: Read-only data (strings, const variables).

**4-Byte Alignment**: Sufficient for:
- ARM instructions (4 bytes)
- Most data types
- No MMU page requirements (simpler than QEMU)

**No Execute Protection**: Without program headers, the Pi bootloader doesn't enforce W^X at load time. MMU setup in `mmu.c` handles this later.

### .data Section (Lines 18-21)

```ld
.data : ALIGN(4)
{
    *(.data*)
}
```

**Purpose**: Initialized global/static variables.

**Simpler Alignment**: Only 4-byte alignment needed (vs 4KB in QEMU).

**Why**: 
- Pi bootloader loads flat binary, not ELF with segments
- MMU setup happens later in C code
- Initial boot doesn't require page alignment

### .bss Section (Lines 23-29)

```ld
.bss : ALIGN(4)
{
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
}
```

**Purpose**: Uninitialized data, zeroed by `start.s`.

**Symbols**:
- `__bss_start`: Beginning of BSS (used in start.s)
- `__bss_end`: End of BSS (used in start.s)

**COMMON Section**: Holds tentatively defined symbols (C variables declared without `extern` or `static`).

**Same as QEMU**: Boot code uses identical BSS zeroing logic regardless of platform.

### Stack Setup (Lines 31-32)

```ld
. = ALIGN(16);
stack_top = . + 0x4000;
```

**Purpose**: Defines initial kernel stack location and size.

**Key Points**:
- **16-byte alignment**: Required by AArch64 calling convention
- **0x4000 bytes**: 16KB stack (1/4 the size of QEMU version)
- **stack_top**: Symbol used by `start.s` to set SP

**Why Smaller Stack?**

1. **Memory Constraints**: Physical Pi may have less RAM
2. **Simpler Init**: Pi boot is more straightforward, less nesting
3. **Sufficient**: 16KB is adequate for kernel initialization
4. **Tasks Get Own Stacks**: This is just for early boot; tasks allocate their own stacks later

**Stack Math**:
- If BSS ends at 0x00100000
- After 16-byte alignment: 0x00100000
- stack_top = 0x00100000 + 0x4000 = 0x00104000
- Stack ranges from 0x00100000 to 0x00104000

## Binary Format

**QEMU**: ELF format (`kernel8.elf`)
- Contains section headers, program headers
- QEMU's ELF loader parses headers
- Supports sophisticated features (PHDRS, etc.)

**Raspberry Pi**: Flat binary (`kernel8.img`)
- Raw memory image without headers
- Created with: `objcopy -O binary kernel8.elf kernel8.img`
- Bootloader loads entire file to 0x80000
- No segment parsing - just load and jump

## Build Process

### Full Compilation Chain:

```bash
# 1. Compile C and assembly to object files
aarch64-none-elf-gcc -c file.c -o file.o

# 2. Link with Pi linker script to create ELF
aarch64-none-elf-ld -T linkers/linker_pi.ld -o kernel8.elf $(OBJECTS)

# 3. Convert ELF to flat binary for Pi bootloader
aarch64-none-elf-objcopy -O binary kernel8.elf kernel8.img

# 4. Copy kernel8.img to SD card boot partition
cp kernel8.img /mnt/boot/
```

### SD Card Structure:

```
/boot/
  ├── bootcode.bin      (GPU first stage bootloader)
  ├── start.elf         (GPU second stage)
  ├── config.txt        (Boot configuration)
  └── kernel8.img       (Your OS - linked with linker_pi.ld)
```

## Design Rationale

### Why So Simple?

1. **Bootloader Limitations**: Pi bootloader doesn't parse ELF program headers
2. **No Need for Complexity**: Basic layout sufficient for initial boot
3. **MMU Setup Later**: Page alignment done in C code, not at link time
4. **Compatibility**: Works with all Pi models (3, 4, Zero 2)

### Why Not Page-Aligned?

**QEMU** needs 4KB alignment because:
- ELF loader sets up MMU based on program headers
- Separate code/data segments require page boundaries

**Pi** doesn't need it because:
- Bootloader doesn't enable MMU
- Kernel enables MMU in C code after boot
- Can set up page tables to map non-aligned sections

### Trade-offs

**Advantages**:
- Simple and easy to understand
- Smaller memory footprint (less padding)
- Fast linking (fewer constraints)

**Disadvantages**:
- No automatic W^X at boot (kernel must set up)
- No memory protection until MMU enabled
- Harder to debug memory corruption

## Raspberry Pi Specific Considerations

### Load Address Constraints

**Cannot Use**:
- `0x0 - 0x1000`: Interrupt vectors (may be remapped)
- `0x1000 - 0x8000`: GPU workspace
- `0x8000 - 0x80000`: ATAGS, device tree blob

**Can Use**:
- `0x80000+`: Kernel and all RAM

### Memory Limits

Different Pi models have different RAM:
- Pi 3B: 1GB
- Pi 3B+: 1GB
- Pi 4: 1GB / 2GB / 4GB / 8GB
- Pi Zero 2: 512MB

**Design Implication**: Kernel must detect available RAM and adapt memory allocators accordingly.

### Cache Behavior

Without MMU enabled, caches may be off or in an inconsistent state. The kernel must:
1. Invalidate caches early in boot
2. Set up page tables with proper cache attributes
3. Enable MMU with correct memory types

**Cross-reference**: [mmu.c](../kernel/memory/mmu.c.md) handles Pi-specific MMU setup.

## Symbol Exports

Symbols available to C/assembly code:

```c
extern char __bss_start[];
extern char __bss_end[];
extern char stack_top[];
```

**Usage in start.s** (identical for both platforms):
```assembly
ldr x0, =__bss_start
ldr x1, =__bss_end
mov x2, #0
bss_clear:
    cmp x0, x1
    b.ge bss_done
    str x2, [x0], #8
    b bss_clear
```

## Debugging

### Verify Load Address

```bash
# Check entry point
aarch64-none-elf-readelf -h kernel8.elf | grep Entry

# Should show: Entry point address: 0x80000
```

### Check Section Layout

```bash
# Display sections
aarch64-none-elf-objdump -h kernel8.elf

# Look for:
# .text starts at 0x80000
# .rodata, .data, .bss follow in order
```

### Verify Binary Size

```bash
# Binary size should roughly match ELF .text + .rodata + .data
ls -lh kernel8.img

# If too large, check for:
# - Accidentally included debug symbols
# - Large initialized arrays
```

### Boot Problems

**Symptom**: Pi doesn't boot (no UART output)

**Possible Causes**:
1. Wrong load address in linker script
2. Binary not copied to SD card correctly
3. Config.txt doesn't specify kernel8.img
4. BSS symbols wrong (kernel crashes immediately)

**Debugging**:
```bash
# 1. Check symbols
aarch64-none-elf-nm kernel8.elf | grep bss

# 2. Verify _start location
aarch64-none-elf-objdump -d kernel8.elf | grep "_start>"

# 3. Check first instruction
aarch64-none-elf-objdump -d kernel8.elf | head -20
# Should see: mrs x0, CurrentEL
```

## Cross-References

- **Boot Code**: [start.s](start.s.md) - Uses symbols defined here
- **QEMU Linker**: [linker.ld](linker.ld.md) - Comparison with full-featured version
- **MMU Setup**: [mmu.c](../kernel/memory/mmu.c.md) - Sets up page tables for these sections
- **Build System**: [BUILD-SYSTEM.md](../build/BUILD-SYSTEM.md) - How this is used

## Security Implications

**Limited Initial Protection**:
- No W^X until MMU enabled
- Stack is executable until MMU configured
- All memory accessible until permissions set

**Mitigation**: Kernel enables MMU quickly after boot, establishing proper permissions.

## Performance

**Advantages of Simpler Layout**:
- Faster linking (no complex alignment)
- Slightly smaller binary (less padding)
- Lower memory fragmentation

**Disadvantages**:
- May require more TLB entries if sections span multiple pages
- Cache efficiency potentially lower without alignment

## Future Enhancements

- **Separate IRQ Stack**: Add dedicated interrupt stack symbol
- **Per-CPU Stacks**: For multi-core Pi 4 support
- **Guard Pages**: Add unmapped regions around stack
- **KASLR**: Randomize load address (requires bootloader support)

## Historical Notes

The 0x80000 load address has been standard since Raspberry Pi 3 introduced 64-bit mode. Earlier 32-bit Pi models loaded at 0x8000 (with `kernel7.img`). The `kernel8.img` name specifically indicates a 64-bit AArch64 kernel.
