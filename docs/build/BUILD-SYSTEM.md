# MYRASPOS Build System Documentation

## Overview

The MYRASPOS build system is a Windows batch-based system that compiles AArch64 assembly and C code, links them into a kernel ELF executable, and prepares a bootable disk image.

## Build Tools

### Toolchain
- **Compiler**: aarch64-none-elf-gcc (ARM bare-metal GCC)
- **Linker**: aarch64-none-elf-ld
- **Object Copy**: aarch64-none-elf-objcopy
- **Target Architecture**: AArch64 (ARMv8-A 64-bit)
- **ABI**: LP64 (Long and Pointer are 64-bit)

### Directory Structure
```
MYRASPOS/
├── build.bat              # Main build script
├── sim.bat                # Simulator launch script
├── make_disk.py           # Disk image creator
├── aarch64/               # Toolchain wrappers
├── boot/                  # Boot assembly code
├── kernel/                # Kernel source files
├── linkers/               # Linker scripts
├── temp/                  # Build artifacts
│   ├── objects/           # Compiled .o files
│   ├── elfs/              # Linked ELF executables
│   ├── binaries/          # Binary images
│   └── maps/              # Linker map files
├── assets/                # Files to include in disk image
└── disk.img               # Virtual disk (created by build)
```

## Build Process

### Step 1: Disk Image Creation
```batch
python make_disk.py
```

**Purpose**: Creates `disk.img` with embedded assets

**Process**:
1. Scans `assets/` directory for files
2. Creates directory table at sector 1 (128 entries max)
3. Packs file data starting at sector 128
4. Writes 16MB raw disk image

**Directory Entry Format** (72 bytes):
```c
struct disk_entry {
    char name[64];        // Full path (e.g., /system/assets/myra.png)
    uint32_t size;        // File size in bytes
    uint32_t start_sector; // Starting sector number
};
```

**Design Constraints**:
- Maximum 128 files
- 16MB total disk size
- 512-byte sectors
- No subdirectories in assets
- Files prefixed with `/system/assets/`

### Step 2: Object File Cleanup
```batch
del /F /Q temp\objects\*.o
del /F /Q temp\elfs\*.elf
del /F /Q temp\binaries\*.img
del /F /Q temp\maps\*.map
```

**Purpose**: Ensure clean build (no stale objects)

### Step 3: Compiler Flags Setup
```batch
set C_FLAGS=-fno-builtin -fno-merge-constants -fno-common ^
            -mgeneral-regs-only -ffreestanding -nostdlib ^
            -nostartfiles -mcpu=cortex-a53 -march=armv8-a ^
            -mabi=lp64 -Wall -Wextra -Wmissing-prototypes ^
            -Ikernel ^
            -DLODEPNG_NO_COMPILE_ALLOCATORS ^
            -DLODEPNG_NO_COMPILE_DISK
```

#### Flag Explanation

| Flag | Purpose | Constraint |
|------|---------|------------|
| `-fno-builtin` | No compiler intrinsics | Use explicit functions |
| `-fno-merge-constants` | Keep constants separate | Easier debugging |
| `-fno-common` | No common variables | Explicit variable placement |
| `-mgeneral-regs-only` | No NEON/FP registers | Kernel doesn't save FP context |
| `-ffreestanding` | Freestanding environment | No standard library |
| `-nostdlib` | No standard library | Custom memory management |
| `-nostartfiles` | No C runtime startup | Custom boot code (start.s) |
| `-mcpu=cortex-a53` | Target Cortex-A53 | Raspberry Pi 3 / QEMU virt |
| `-march=armv8-a` | ARMv8-A architecture | 64-bit ARM |
| `-mabi=lp64` | LP64 ABI | Standard 64-bit ABI |
| `-Wall -Wextra` | All warnings | Code quality |
| `-Wmissing-prototypes` | Require prototypes | Catch missing declarations |
| `-Ikernel` | Include path | Access kernel headers |
| `-DLODEPNG_NO_COMPILE_ALLOCATORS` | LodePNG config | Use custom allocators |
| `-DLODEPNG_NO_COMPILE_DISK` | LodePNG config | No disk I/O in LodePNG |

**Design Decision**: `-mgeneral-regs-only` is critical because the kernel doesn't save/restore NEON/FPU registers during context switches. Using FP registers would cause corruption across task switches.

### Step 4: Compilation

#### Assembly Files
```batch
call %GCC% %C_FLAGS% -c boot/start.S -o temp/objects/start.o 
call %GCC% %C_FLAGS% -c kernel/vectors.S -o temp/objects/vectors.o
call %GCC% %C_FLAGS% -c kernel/swtch.S -o temp/objects/swtch.o
```

**Files**:
- `start.S`: Boot code, exception level setup
- `vectors.S`: Exception vector table, register save/restore
- `swtch.S`: Context switching assembly

#### Kernel Core
```batch
call %GCC% %C_FLAGS% -c kernel/kernel.c -o temp/objects/kernel.o
call %GCC% %C_FLAGS% -c kernel/init.c -o temp/objects/init.o
call %GCC% %C_FLAGS% -c kernel/sched.c -o temp/objects/sched.o
call %GCC% %C_FLAGS% -c kernel/panic.c -o temp/objects/panic.o
```

#### Memory Management
```batch
call %GCC% %C_FLAGS% -c kernel/palloc.c -o temp/objects/palloc.o
call %GCC% %C_FLAGS% -c kernel/kmalloc.c -o temp/objects/kmalloc.o
call %GCC% %C_FLAGS% -c kernel/mmu.c -o temp/objects/mmu.o
```

#### I/O Subsystems
```batch
call %GCC% %C_FLAGS% -c kernel/uart.c -o temp/objects/uart.o
call %GCC% %C_FLAGS% -c kernel/timer.c -o temp/objects/timer.o
call %GCC% %C_FLAGS% -c kernel/irq.c -o temp/objects/irq.o
call %GCC% %C_FLAGS% -c kernel/framebuffer.c -o temp/objects/framebuffer.o
call %GCC% %C_FLAGS% -c kernel/input.c -o temp/objects/input.o
```

#### Filesystems
```batch
call %GCC% %C_FLAGS% -c kernel/ramfs.c -o temp/objects/ramfs.o
call %GCC% %C_FLAGS% -c kernel/diskfs.c -o temp/objects/diskfs.o
call %GCC% %C_FLAGS% -c kernel/files.c -o temp/objects/files.o
```

#### Drivers
```batch
call %GCC% %C_FLAGS% -c kernel/virtio.c -o temp/objects/virtio.o
```

#### Services
```batch
call %GCC% %C_FLAGS% -c kernel/shell.c -o temp/objects/shell.o
call %GCC% %C_FLAGS% -c kernel/pty.c -o temp/objects/pty.o
call %GCC% %C_FLAGS% -c kernel/service.c -o temp/objects/service.o
call %GCC% %C_FLAGS% -c kernel/programs.c -o temp/objects/programs.o
```

#### Window Manager
```batch
call %GCC% %C_FLAGS% -c kernel/wm.c -o temp/objects/wm.o
call %GCC% %C_FLAGS% -c kernel/cursor.c -o temp/objects/cursor.o
```

#### Libraries
```batch
call %GCC% %C_FLAGS% -c kernel/lib.c -o temp/objects/lib.o
call %GCC% %C_FLAGS% -c kernel/glob.c -o temp/objects/glob.o
call %GCC% %C_FLAGS% -c kernel/syscall.c -o temp/objects/syscall.o
call %GCC% %C_FLAGS% -c kernel/write.c -o temp/objects/write.o
call %GCC% %C_FLAGS% -c kernel/lodepng.c -o temp/objects/lodepng.o
call %GCC% %C_FLAGS% -c kernel/lodepng_glue.c -o temp/objects/lodepng_glue.o
call %GCC% %C_FLAGS% -c kernel/image.c -o temp/objects/image.o
```

#### Commands (24 total)
```batch
call %GCC% %C_FLAGS% -c kernel/commands/cat.c -o temp/objects/cat.o
call %GCC% %C_FLAGS% -c kernel/commands/clear.c -o temp/objects/clear.o
call %GCC% %C_FLAGS% -c kernel/commands/cp.c -o temp/objects/cp.o
call %GCC% %C_FLAGS% -c kernel/commands/echo.c -o temp/objects/echo.o
call %GCC% %C_FLAGS% -c kernel/commands/edit.c -o temp/objects/edit.o
call %GCC% %C_FLAGS% -c kernel/commands/free.c -o temp/objects/free.o
call %GCC% %C_FLAGS% -c kernel/commands/grep.c -o temp/objects/grep.o
call %GCC% %C_FLAGS% -c kernel/commands/head.c -o temp/objects/head.o
call %GCC% %C_FLAGS% -c kernel/commands/help.c -o temp/objects/help.o
call %GCC% %C_FLAGS% -c kernel/commands/kill.c -o temp/objects/kill.o
call %GCC% %C_FLAGS% -c kernel/commands/ls.c -o temp/objects/ls.o
call %GCC% %C_FLAGS% -c kernel/commands/mkdir.c -o temp/objects/mkdir.o
call %GCC% %C_FLAGS% -c kernel/commands/more.c -o temp/objects/more.o
call %GCC% %C_FLAGS% -c kernel/commands/mv.c -o temp/objects/mv.o
call %GCC% %C_FLAGS% -c kernel/commands/ps.c -o temp/objects/ps.o
call %GCC% %C_FLAGS% -c kernel/commands/ramfs_tools.c -o temp/objects/ramfs_tools.o
call %GCC% %C_FLAGS% -c kernel/commands/rm.c -o temp/objects/rm.o
call %GCC% %C_FLAGS% -c kernel/commands/rmdir.c -o temp/objects/rmdir.o
call %GCC% %C_FLAGS% -c kernel/commands/sleep.c -o temp/objects/sleep.o
call %GCC% %C_FLAGS% -c kernel/commands/systemctl.c -o temp/objects/systemctl.o
call %GCC% %C_FLAGS% -c kernel/commands/tail.c -o temp/objects/tail.o
call %GCC% %C_FLAGS% -c kernel/commands/touch.c -o temp/objects/touch.o
call %GCC% %C_FLAGS% -c kernel/commands/tree.c -o temp/objects/tree.o
call %GCC% %C_FLAGS% -c kernel/commands/view.c -o temp/objects/view.o
call %GCC% %C_FLAGS% -c kernel/commands/wait.c -o temp/objects/wait.o
```

#### Applications (7 total)
```batch
call %GCC% %C_FLAGS% -c kernel/apps/calculator_app.c -o temp/objects/calculator_app.o
call %GCC% %C_FLAGS% -c kernel/apps/editor_app.c -o temp/objects/editor_app.o
call %GCC% %C_FLAGS% -c kernel/apps/files_app.c -o temp/objects/files_app.o
call %GCC% %C_FLAGS% -c kernel/apps/image_viewer.c -o temp/objects/image_viewer.o
call %GCC% %C_FLAGS% -c kernel/apps/keyboard_tester_app.c -o temp/objects/keyboard_tester_app.o
call %GCC% %C_FLAGS% -c kernel/apps/myra_app.c -o temp/objects/myra_app.o
call %GCC% %C_FLAGS% -c kernel/apps/terminal_app.c -o temp/objects/terminal_app.o
```

### Step 5: Linking

```batch
call "aarch64/aarch64-none-elf-ld.bat" [all .o files] ^
    -T linkers/linker.ld ^
    -o temp/elfs/kernel.elf ^
    -Map temp/maps/kernel.map
```

**Linker Script**: `linkers/linker.ld`

**Key Sections**:
```ld
ENTRY(_start)

SECTIONS {
    . = 0x40000000;           /* Kernel loads at 1GB */
    
    .text : { *(.text*) }     /* Code section */
    .rodata : { *(.rodata*) } /* Read-only data */
    .data : { *(.data*) }     /* Initialized data */
    .bss : {                  /* Uninitialized data */
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
    
    . = ALIGN(0x1000);        /* Page align stack */
    stack_top = . + 0x100000; /* 1MB stack */
}
```

**Design Decisions**:
- Kernel loaded at 0x40000000 (1GB physical address)
- All sections in order: text → rodata → data → bss
- 1MB stack allocated after BSS
- Page-aligned for MMU
- `_start` is entry point from start.S

**Map File**: `temp/maps/kernel.map`
- Contains symbol addresses
- Useful for debugging panics (symbol lookup)
- Shows memory layout

### Step 6: Binary Extraction

```batch
call "aarch64/aarch64-none-elf-objcopy.bat" ^
    -O binary ^
    temp/elfs/kernel.elf ^
    temp/binaries/kernel8.img
```

**Purpose**: Extract raw binary from ELF (for direct loading)

**Output**: `kernel8.img`
- Raw binary machine code
- No ELF headers
- Can be loaded directly at 0x40000000

**Note**: QEMU can load ELF directly, so binary not strictly needed but kept for compatibility.

## Build Artifacts

### Generated Files
```
temp/objects/*.o         # Object files (52 files)
temp/elfs/kernel.elf     # Linked kernel executable
temp/binaries/kernel8.img # Raw binary
temp/maps/kernel.map     # Linker symbol map
disk.img                 # Virtual disk with assets
```

### File Sizes (Approximate)
- Object files: ~2-50KB each
- kernel.elf: ~500KB
- kernel8.img: ~500KB
- disk.img: 16MB (sparse)
- kernel.map: ~200KB

## Build Dependencies

### Required Files
- All .c and .h files in kernel/
- All .S files in boot/ and kernel/
- Linker scripts in linkers/
- Assets in assets/ directory

### Tool Dependencies
- aarch64-none-elf toolchain (GCC 10.3 or later)
- Python 3.x (for make_disk.py)
- Windows batch support

## Build Time

- Clean build: ~30-60 seconds (Windows)
- Incremental: Not supported (always clean)
- Parallel builds: Not supported

## Optimization Flags

**Not Used**: `-O0` through `-O3`

**Reason**: Debugging clarity prioritized over performance. Optimizations can:
- Reorder code
- Inline functions
- Remove dead code
- Make debugging harder

**Future Consideration**: `-O2` for production builds

## Debug Information

**Not Included**: `-g` flag not used

**Reason**: 
- Increases binary size significantly
- QEMU GDB debugging not primary workflow
- UART logging is primary debug method

**To Enable**: Add `-g` to C_FLAGS for GDB support

## Cross-Compilation Wrappers

### aarch64-none-elf-gcc.bat
```batch
@echo off
"C:\path\to\gcc\bin\aarch64-none-elf-gcc.exe" %*
```

**Purpose**: Wrapper for portability across systems

**Design**: Allows changing toolchain path without modifying build.bat

## Alternative Targets

### Raspberry Pi 3 (Commented Out)
```batch
REM call "arm-none/arm-none-eabi-ld.bat" ... ^
REM     -T linkers/linker_pi.ld ...
REM call "arm-none/arm-none-eabi-objcopy.bat" ...
```

**Difference**: Uses `arm-none-eabi` (32-bit) toolchain and `linker_pi.ld`

**Status**: Not actively maintained; focus is QEMU virt

## Build Constraints

### Hard Constraints
1. Must run on Windows (batch scripts)
2. Must have aarch64-none-elf toolchain installed
3. Must have Python 3 for disk creation
4. All source files must compile without errors
5. Total binary must fit in 512MB RAM

### Soft Constraints
1. Build time should be < 2 minutes
2. Disk image should be < 50MB
3. Object files in temp/ can be cleaned safely
4. No external dependencies beyond toolchain

## Build Troubleshooting

### Common Issues

1. **"gcc not found"**
   - Check aarch64/ wrapper paths
   - Ensure toolchain installed
   - Verify PATH

2. **"Undefined reference to X"**
   - Missing object file in link command
   - Check build.bat includes all .o files
   - Verify function is defined

3. **"disk.img creation failed"**
   - Check Python installed
   - Verify assets/ directory exists
   - Check disk space (16MB needed)

4. **"Relocation truncated to fit"**
   - Binary too large for memory layout
   - Check linker script addresses
   - Reduce code size

## Build System Design Decisions

### Why Batch Files?
- **Simplicity**: Easy to understand and modify
- **No dependencies**: No make, cmake, or build system needed
- **Explicit**: Every step visible
- **Platform**: Original development on Windows

**Trade-offs**:
- Not portable to Linux/Mac without modification
- No incremental builds (always full rebuild)
- No parallel compilation
- Slower than make-based systems

### Why Python for Disk Creation?
- **Struct packing**: Easy binary data manipulation
- **Cross-platform**: Python available on all systems
- **Simple**: ~100 lines of code
- **Flexible**: Easy to add more files

### Why Clean Build Every Time?
- **Reliability**: No stale object issues
- **Simplicity**: No dependency tracking needed
- **Small project**: Build time acceptable (~1 min)

**Trade-off**: Longer builds, but avoids subtle bugs

## Future Improvements

### Potential Enhancements
1. **Makefile**: Support incremental builds
2. **CMake**: Cross-platform build system
3. **Parallel**: Compile files in parallel
4. **Debug builds**: Separate debug/release targets
5. **Size optimization**: `-Os` for smaller binary
6. **LTO**: Link-time optimization

### Not Planned
- Autoconf/Automake (too complex)
- Meson (adds dependency)
- Bazel (overkill for project size)

---

**See Also**:
- [sim.bat Documentation](sim.bat.md)
- [Python Scripts](python-scripts.md)
- [Linker Scripts](../boot/linker-files.md)
