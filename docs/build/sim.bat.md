# MYRASPOS Simulator (sim.bat) Documentation

## Overview

The `sim.bat` script builds the kernel and launches it in QEMU (Quick Emulator) with a configured virtual machine environment that simulates an AArch64 system suitable for running MYRASPOS.

## Script Contents

```batch
call build.bat

echo Launching QEMU with SDL + ramfb (fallback) and virtio-gpu

call "qemu/qemu-system-aarch64.bat" ^
  -machine virt ^
  -cpu cortex-a53 ^
  -m 512M ^
  -display sdl ^
  -device virtio-gpu-device ^
  -device virtio-mouse-device ^
  -device virtio-keyboard-device ^
  -device ramfb ^
  -drive if=none,file=disk.img,id=hd0,format=raw ^
  -device virtio-blk-device,drive=hd0 ^
  -kernel temp/elfs/kernel.elf ^
  -serial mon:stdio
```

## Execution Flow

### 1. Build Step
```batch
call build.bat
```

**Purpose**: Compile and link the kernel before running

**Actions**:
- Creates disk.img with assets
- Compiles all source files
- Links kernel.elf
- Creates kernel8.img binary

**Result**: Fresh kernel ready to boot

### 2. Launch QEMU
Starts QEMU with configured virtual hardware

## QEMU Configuration

### Machine Type
```batch
-machine virt
```

**Purpose**: Use QEMU's generic virtualization platform

**Details**:
- **Platform**: `virt` - Generic AArch64 virtualization platform
- **Features**: GICv2, PL011 UART, virtio devices
- **Memory map**: Standard virt machine layout
- **Devices**: PCIe, virtio-mmio, GPIO, RTC

**Design Decision**: The `virt` machine provides a clean, well-defined hardware interface that's easier to program than real hardware (like Raspberry Pi) while being representative of real ARM systems.

**Memory Layout** (virt machine):
```
0x00000000 - 0x08000000   128MB   Flash/ROM (not used)
0x08000000 - 0x08010000   64KB    GIC Distributor
0x08010000 - 0x08020000   64KB    GIC CPU Interface  
0x09000000 - 0x09001000   4KB     PL011 UART
0x0A000000 - 0x0A000200   512B    VirtIO MMIO (first device)
0x0A000200 - 0x0A000400   512B    VirtIO MMIO (second device)
...
0x40000000 - 0x60000000   512MB   RAM (our configuration)
```

### CPU Type
```batch
-cpu cortex-a53
```

**Purpose**: Emulate ARM Cortex-A53 processor

**Details**:
- **Architecture**: ARMv8-A (64-bit)
- **Features**: 
  - Generic Timer
  - GICv2 interrupt controller
  - TrustZone (not used)
  - Crypto extensions (not used)
- **Registers**: 31 general-purpose (x0-x30) + SP, PC
- **Exception Levels**: EL0-EL3 (kernel runs at EL1)

**Design Decision**: Cortex-A53 is used in Raspberry Pi 3, making this configuration representative of real hardware we might target.

**Compatibility**: Code runs on both QEMU virt (Cortex-A53) and real Raspberry Pi 3.

### Memory Size
```batch
-m 512M
```

**Purpose**: Allocate 512MB of RAM

**Details**:
- **Physical Range**: 0x40000000 - 0x60000000
- **Usage**:
  - Kernel code/data: ~0x40000000 - 0x42000000 (32MB)
  - Heap: 0x43000000 - 0x60000000 (464MB)
  - Stack: 0x60000000 (grows down)

**Design Constraint**: 
- Minimum: 256MB (for basic operation)
- Recommended: 512MB (for applications)
- Maximum: Limited by QEMU and linker script

**Trade-off**: More memory allows more tasks and larger buffers, but makes the system less representative of embedded constraints.

### Display
```batch
-display sdl
```

**Purpose**: Use SDL for graphics output

**Details**:
- **Backend**: Simple DirectMedia Layer (SDL2)
- **Window**: Resizable, hardware accelerated
- **Advantages**: 
  - Interactive (mouse/keyboard)
  - Fast rendering
  - Cross-platform

**Alternatives**:
- `-display gtk`: GTK-based display (more features)
- `-display vnc=:0`: VNC server (remote access)
- `-nographic`: No graphics (serial only)

**Design Decision**: SDL provides the best balance of simplicity, performance, and cross-platform support for development.

### Graphics Devices

#### VirtIO GPU
```batch
-device virtio-gpu-device
```

**Purpose**: Primary graphics device

**Details**:
- **Type**: VirtIO GPU (paravirtualized)
- **Features**:
  - Efficient host-guest communication
  - Framebuffer sharing
  - 2D acceleration
  - Resource management
- **Configuration**:
  - Default resolution: 1280x720
  - 32-bit RGBA format
  - Double buffering support

**Advantages**:
- Fast (shared memory)
- Well-defined protocol
- Scanout to host display

**Implementation**: Handled by `virtio.c` and `framebuffer.c`

#### RAMFB (Fallback)
```batch
-device ramfb
```

**Purpose**: Simple fallback framebuffer

**Details**:
- **Type**: RAMFramebuffer (fixed memory region)
- **Address**: 0x42000000 (in QEMU virt)
- **Size**: Depends on resolution (e.g., 1280x720x4 = 3.6MB)
- **Format**: 32-bit RGBA

**Design Decision**: RAMFB provides a simple, always-available graphics option if VirtIO GPU initialization fails.

**Initialization Order**:
1. Try VirtIO GPU (preferred)
2. If failed, fall back to RAMFB
3. If both fail, panic

### Input Devices

#### VirtIO Mouse
```batch
-device virtio-mouse-device
```

**Purpose**: Mouse input handling

**Details**:
- **Type**: VirtIO input device (mouse)
- **Events**: Movement (dx, dy), button presses
- **Buttons**: 3 buttons (left, middle, right)
- **Queue**: Event queue in shared memory

**Implementation**: Handled by `input.c` via VirtIO protocol

#### VirtIO Keyboard
```batch
-device virtio-keyboard-device
```

**Purpose**: Keyboard input handling

**Details**:
- **Type**: VirtIO input device (keyboard)
- **Events**: Key presses, key releases
- **Scancode**: USB HID keycodes
- **Queue**: Event queue in shared memory

**Implementation**: Handled by `input.c` via VirtIO protocol

**Design Decision**: VirtIO input devices provide a clean, standardized interface compared to PS/2 emulation, with better support for modern input features.

### Block Device

#### VirtIO Block
```batch
-drive if=none,file=disk.img,id=hd0,format=raw
-device virtio-blk-device,drive=hd0
```

**Purpose**: Persistent storage (virtual disk)

**Details**:
- **Backend**: `disk.img` (16MB raw image)
- **Interface**: VirtIO block device
- **Operations**: Read, write sectors (512 bytes)
- **Performance**: Synchronous (blocking I/O)

**Drive Configuration**:
- `if=none`: No direct attachment (used by device)
- `file=disk.img`: Disk image file
- `id=hd0`: Internal identifier
- `format=raw`: Raw disk format (no filesystem)

**Device Configuration**:
- `drive=hd0`: Link to drive backend
- Type: virtio-blk-device (platform device, not PCI)

**Design Decision**: VirtIO block provides simple, efficient disk I/O suitable for embedded systems.

**Usage**:
- Boot: Kernel loads assets from disk
- Runtime: DiskFS reads/writes files
- Persistence: Changes persist across reboots

### Kernel Loading
```batch
-kernel temp/elfs/kernel.elf
```

**Purpose**: Load kernel into memory and start execution

**Details**:
- **Format**: ELF executable
- **Load Address**: 0x40000000 (defined in linker script)
- **Entry Point**: `_start` symbol (from start.S)
- **Loading**: QEMU loads segments into RAM
- **Execution**: Jump to entry point in EL1

**Initialization**:
1. QEMU loads ELF segments to specified addresses
2. Sets PC (program counter) to entry point
3. Sets CPU to EL1 (kernel mode)
4. Starts execution at `_start`

**Design Decision**: ELF format is preferred over raw binary because:
- Contains metadata (load address, entry point)
- More flexible than raw binary
- Easier debugging (symbol table)

### Serial Console
```batch
-serial mon:stdio
```

**Purpose**: Serial console for kernel output and debugging

**Details**:
- **Device**: PL011 UART @ 0x09000000
- **Backend**: stdio (terminal standard I/O)
- **Monitor**: QEMU monitor commands available (Ctrl+A, C)
- **Baud rate**: Not applicable (virtual)

**Console Features**:
- **Output**: Kernel UART output to terminal
- **Input**: Terminal input to kernel (if supported)
- **Monitor**: QEMU command console (system inspection)

**Monitor Commands** (Ctrl+A, C to access):
- `info registers`: Show CPU registers
- `info mem`: Show memory mappings
- `quit`: Exit QEMU
- `system_reset`: Restart kernel

**Design Decision**: Serial console is essential for:
- Boot diagnostics
- Kernel debugging
- System logging
- Development workflow

**UART Output Example**:
```
[kernel] initializing memory subsystems...
[kernel] enabling MMU...
[kernel] initializing ramfs...
[kernel] initializing diskfs...
...
```

## Alternative Configurations (Commented Out)

### ARM 32-bit (Not Used)
```batch
REM call "qemu/qemu-system-arm.bat" ...
```

**Reason**: Project targets 64-bit AArch64, not 32-bit ARM

### No Graphics (Not Used)
```batch
REM -nographic
```

**Reason**: Window manager and GUI applications require graphics

### Different Display (Not Used)
```batch
REM -display gtk
```

**Reason**: SDL is simpler and more portable

## QEMU Version Requirements

### Minimum Version
- QEMU 5.0 or later

### Recommended Version
- QEMU 6.0 or later

### Required Features
- AArch64 emulation
- virt machine support
- VirtIO devices (GPU, block, input)
- RAMFB support
- SDL display backend

## Running the Simulator

### Windows
```batch
sim.bat
```

### Expected Output
```
Creating disk.img...
Created disk.img with X files.
[Compilation output...]
Launching QEMU with SDL + ramfb (fallback) and virtio-gpu
[QEMU window opens]
[Serial console shows kernel boot messages]
```

### Stopping Simulation
- Close QEMU window
- Press Ctrl+C in terminal
- Use QEMU monitor: Ctrl+A, C, then `quit`

## Debugging Features

### UART Logging
All kernel messages appear in the terminal:
```
[kernel] initializing...
[sched] creating task 'init'
[init] starting services...
```

### QEMU Monitor
Access via Ctrl+A, C:
- Inspect CPU state
- View memory
- Set breakpoints (with GDB)

### Framebuffer Display
Visual confirmation of:
- Graphics initialization
- Window manager operation
- Application rendering

## Performance Characteristics

### Boot Time
- Typical: 1-2 seconds to shell
- Fast: QEMU doesn't emulate real-time delays

### Graphics Performance
- 60 FPS target (not enforced)
- Limited by host CPU
- SDL rendering overhead

### Disk I/O
- Fast: QEMU caches disk image
- Synchronous: Blocking operations
- No seek delays

## Common Issues

### Issue: "QEMU not found"
**Solution**: 
- Install QEMU for Windows
- Update path in qemu/qemu-system-aarch64.bat
- Verify installation: `qemu-system-aarch64 --version`

### Issue: "SDL not available"
**Solution**:
- Install SDL2 libraries
- Use alternative display: `-display gtk` or `-nographic`

### Issue: "VirtIO GPU not initialized"
**Symptom**: Falls back to RAMFB
**Solution**: 
- Update QEMU to newer version
- Check QEMU compile flags (VirtIO GPU enabled)

### Issue: "Kernel doesn't boot"
**Symptoms**: Blank screen, no UART output
**Solutions**:
- Check build.bat completed successfully
- Verify kernel.elf exists
- Check linker script (correct addresses)
- Try `-nographic` to see panic messages

### Issue: "Disk not found"
**Symptom**: DiskFS init fails
**Solution**:
- Ensure disk.img exists
- Check file size (should be 16MB)
- Verify disk.img in current directory

## Design Decisions

### Why QEMU?
- **Fast**: No hardware needed
- **Reproducible**: Same environment everywhere
- **Debuggable**: GDB integration, monitor
- **Available**: Cross-platform, open source

**Trade-offs**:
- Not 100% accurate (timing differences)
- Some hardware features not emulated
- Performance not representative

### Why virt Machine?
- **Simple**: Clean hardware model
- **Standard**: Well-documented
- **Flexible**: Easy to add devices
- **Portable**: Works on all QEMU versions

**Trade-offs**:
- Not real hardware (Raspberry Pi different)
- Some peripherals unavailable
- Memory map different from Pi

### Why 512MB RAM?
- **Sufficient**: Enough for applications
- **Realistic**: Embedded systems constraint
- **Fast**: Quick boot, low overhead

**Trade-offs**:
- Less than typical desktop (GBs)
- More than minimal embedded (MBs)
- Requires careful memory management

## Future Enhancements

### Potential Additions
1. **GDB Integration**: `-s -S` for debugging
2. **Networking**: `-netdev user` for TCP/IP
3. **Multiple cores**: `-smp 4` for SMP testing
4. **Snapshot**: `-snapshot` for non-persistent runs
5. **Tracing**: QEMU tracing for performance analysis

### Not Planned
- Real Raspberry Pi boot (out of scope)
- Hypervisor mode (EL2 not used)
- Secure world (TrustZone not used)

---

**See Also**:
- [Build System](BUILD-SYSTEM.md)
- [Boot Sequence](../01-BOOT-SEQUENCE.md)
- [QEMU virt machine documentation](https://www.qemu.org/docs/master/system/arm/virt.html)
