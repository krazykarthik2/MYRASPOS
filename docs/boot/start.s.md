# Boot Assembly - start.s

## Overview

`start.s` is the **first code executed** when MYRASPOS boots. This AArch64 assembly file is responsible for:
- Setting up the initial execution environment
- Configuring exception handling
- Initializing the stack pointer
- Zeroing the BSS segment
- Transferring control to the C kernel

The bootloader places this code at the entry point defined in the linker script, and execution begins at the `_start` symbol.

## Memory Layout

The `.text.boot` section ensures this code is placed at the beginning of the executable, making it the first code to execute after the bootloader hands over control.

## Boot Sequence

### 1. Exception Level Check (Lines 14-24)

```assembly
mrs x0, CurrentEL
lsr x0, x0, #2
cmp x0, #1
beq 1f
```

**Purpose**: Ensures the CPU is running in Exception Level 1 (EL1), the kernel privilege level.

**Design Decision**: QEMU's `virt` machine starts in EL1 by default, but the code defensively handles EL2 (hypervisor mode) by dropping to EL1 using `eret`.

**Why**: AArch64 has multiple privilege levels (EL0-EL3). The kernel must run in EL1 to access system resources while maintaining separation from user code (EL0).

### 2. Interrupt Masking (Lines 30)

```assembly
msr daifset, #0xf
```

**Purpose**: Disables all interrupts (Debug, SError, IRQ, FIQ) during boot.

**Why**: 
- The interrupt handlers aren't set up yet
- Memory management isn't initialized
- Prevents spurious interrupts from crashing the system during early boot

### 3. Exception Vector Setup (Lines 35-36)

```assembly
ldr x0, =vectors
msr vbar_el1, x0
```

**Purpose**: Points the Vector Base Address Register (VBAR_EL1) to the exception vector table.

**Cross-reference**: The `vectors` symbol is defined in [vectors.S](../kernel/services/vectors.S.md), which contains handlers for synchronous exceptions, IRQs, FIQs, and SErrors.

**Design Decision**: This is done early (before enabling interrupts) to ensure handlers are in place if any exception occurs.

### 4. Stack Initialization (Lines 46-47)

```assembly
ldr x0, =stack_top
mov sp, x0
```

**Purpose**: Sets up the initial kernel stack pointer.

**Stack Requirements**:
- Must be 16-byte aligned (AArch64 requirement)
- Size: 64KB (0x10000 bytes) as defined in linker script
- Located after BSS section

**Why**: C code requires a valid stack for:
- Function calls (storing return addresses)
- Local variables
- Passing arguments beyond register capacity

### 5. BSS Zeroing (Lines 52-62)

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

**Purpose**: Zeroes the BSS (Block Started by Symbol) segment.

**Why**: 
- C standard requires uninitialized global/static variables to be zero
- BSS contains no data in the executable (saves space)
- Must be zeroed at runtime before C code executes

**Implementation**: Simple loop that writes zero in 8-byte increments (optimal for 64-bit architecture).

### 6. Kernel Entry (Lines 66)

```assembly
bl kernel_main
```

**Purpose**: Branches with link to the C kernel entry point.

**Cross-reference**: See [kernel.c](../kernel/core/kernel.c.md) for the kernel initialization sequence.

### 7. Halt Loop (Lines 68-70)

```assembly
halt:
    wfi
    b halt
```

**Purpose**: If `kernel_main` returns (which should never happen), enter an infinite low-power wait loop.

**WFI Instruction**: "Wait For Interrupt" - puts the CPU in low-power state until an interrupt occurs, then continues the loop.

## Register Usage

Following AArch64 calling conventions:
- **x0-x18**: Temporary registers (caller-saved)
- **sp**: Stack pointer (must be 16-byte aligned)
- **x30 (lr)**: Link register (stores return address for `bl`)

The boot code uses x0-x2 as temporary registers, which is safe since nothing has been called yet.

## Memory Regions

The boot code references several symbols from the linker script:

| Symbol | Purpose | Defined In |
|--------|---------|------------|
| `_start` | Entry point | start.s |
| `vectors` | Exception vector table base | vectors.S |
| `stack_top` | Top of initial kernel stack | linker.ld |
| `__bss_start` | BSS segment start | linker.ld |
| `__bss_end` | BSS segment end | linker.ld |

## Design Constraints

1. **Position Independence**: Code assumes it's loaded at the address specified in linker script (0x40800000 for QEMU, 0x80000 for Pi)

2. **No Dependencies**: Cannot use C library or kernel services - must be pure assembly

3. **Minimal Work**: Does only what's absolutely necessary to enter C code safely

4. **No MMU**: Runs with identity mapping initially; MMU setup happens in C code

5. **Single Core**: Assumes single-core boot; multi-core would need core ID checks and separate stacks

## Platform Differences

The same `start.s` works for both QEMU and Raspberry Pi, but different linker scripts define different load addresses:

- **QEMU (linker.ld)**: 0x40800000 - High address to avoid framebuffer collision
- **Pi (linker_pi.ld)**: 0x80000 - Standard Raspberry Pi kernel load address

## Exception Handling Note

Lines 38-40 show commented-out code to unmask exceptions:

```assembly
/* MASKED: We are polling IO, and unhandled IRQs cause hang */
/* msr daifclr, #0xf */
```

**Design Decision**: Interrupts remain masked during boot. They're enabled later after:
- GIC (Generic Interrupt Controller) is initialized
- IRQ handlers are registered
- Memory management is set up

## Cross-References

- **Next Step**: [kernel.c - kernel_main()](../kernel/core/kernel.c.md)
- **Exception Vectors**: [vectors.S](../kernel/services/vectors.S.md)
- **Linker Scripts**: [linker.ld](linker.ld.md), [linker_pi.ld](linker_pi.ld.md)
- **Stack Usage**: Used by all kernel code after boot
- **BSS Symbols**: Used by memory allocators

## Security Considerations

1. **Stack Overflow Risk**: 64KB stack shared by all kernel initialization code - careful not to use excessive stack space

2. **No Stack Canaries**: Early boot has no protection against stack buffer overflows

3. **Exception Level**: Dropping from EL2→EL1 is one-way; cannot regain hypervisor privileges

## Debugging Tips

If the system hangs during boot:
1. Check that `_start` is at the correct load address
2. Verify BSS symbols are properly defined in linker script
3. Ensure stack_top is 16-byte aligned
4. Check that `vectors` symbol is defined and linked
5. Add UART debug output early in `kernel_main` to confirm entry

## Performance Notes

- **BSS Zeroing**: ~10-100μs for typical BSS size (depends on size and cache state)
- **Boot Time**: Total assembly boot sequence: <1μs (most time spent in C initialization)

## Historical Notes

The code includes defensive EL2 handling even though QEMU boots directly to EL1. This was added for compatibility with bare-metal hardware where the bootloader might start in EL2.
