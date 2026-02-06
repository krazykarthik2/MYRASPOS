# UART Driver Documentation (uart.c/h)

## Overview

The UART (Universal Asynchronous Receiver/Transmitter) driver provides serial communication capabilities for MYRASPOS. It implements a PL011 UART driver that interfaces with the ARM PrimeCell UART (PL011) hardware, enabling text-based I/O for debugging, console output, and kernel panic messages.

**Hardware Interface**: ARM PrimeCell UART (PL011)  
**Base Address**: `0x09000000` (QEMU 'virt' machine)  
**Primary Use**: Serial console, kernel debugging, panic output

## Hardware Details

### Register Layout

The driver accesses two primary PL011 registers via memory-mapped I/O:

| Register | Offset | Address | Description |
|----------|--------|---------|-------------|
| UART_DR | 0x00 | 0x09000000 | Data Register - read/write data |
| UART_FR | 0x18 | 0x09000018 | Flag Register - status flags |

### Flag Register (UART_FR) Bits

| Bit | Name | Description |
|-----|------|-------------|
| 4 | RXFE | Receive FIFO Empty - 1 when no data available |
| 5 | TXFF | Transmit FIFO Full - 1 when transmit buffer full |

### Memory-Mapped I/O Access

```c
static inline uint32_t mmio_read(uintptr_t reg) {
    return *(volatile uint32_t *)reg;
}

static inline void mmio_write(uintptr_t reg, uint32_t val) {
    *(volatile uint32_t *)reg = val;
}
```

The `volatile` qualifier ensures the compiler doesn't optimize away hardware accesses.

## Key Functions

### Character Output

#### `void uart_putc(char c)`
**Purpose**: Write a single character to the UART  
**Signature**: `void uart_putc(char c)`  
**Parameters**:
- `c` - Character to transmit

**Behavior**:
- Polls UART_FR register bit 5 (TXFF) until transmit FIFO is not full
- Writes character to UART_DR register
- Blocks until transmission is possible (busy-wait)

**Implementation**:
```c
void uart_putc(char c) {
    while (mmio_read(UART_FR) & (1 << 5)); /* TXFF bit 5 */
    mmio_write(UART_DR, (unsigned int)c);
}
```

#### `void uart_puts(const char *s)`
**Purpose**: Write a null-terminated string to UART  
**Signature**: `void uart_puts(const char *s)`  
**Parameters**:
- `s` - Null-terminated string to transmit

**Behavior**:
- Iterates through string calling `uart_putc()` for each character
- Automatically converts `\n` to `\r\n` (CR+LF) for terminal compatibility
- Terminates when null character encountered

### Character Input

#### `char uart_getc(void)`
**Purpose**: Read a single character from UART (blocking)  
**Signature**: `char uart_getc(void)`  
**Returns**: Character read from UART (8-bit, masked to 0xFF)

**Behavior**:
- Polls UART_FR register bit 4 (RXFE) until data available
- Yields CPU to scheduler while waiting (if `NO_SCHED` not defined)
- Reads character from UART_DR register
- Masks to 8 bits to ensure clean character data

**Implementation**:
```c
char uart_getc(void) {
    /* FR bit 4 == RXFE (receive FIFO empty) */
    while (mmio_read(UART_FR) & (1 << 4)) {
#ifndef NO_SCHED
        extern void yield(void);
        yield();
#endif
    }
    unsigned int v = mmio_read(UART_DR);
    return (char)(v & 0xFF);
}
```

#### `int uart_haschar(void)`
**Purpose**: Check if input data is available (non-blocking)  
**Signature**: `int uart_haschar(void)`  
**Returns**: 1 if data available, 0 if FIFO empty

**Behavior**:
- Checks UART_FR bit 4 (RXFE) without blocking
- Used by IRQ polling mechanism to check for UART input

### Numeric Output

#### `void uart_putu(unsigned int u)`
**Purpose**: Output unsigned integer in decimal format  
**Signature**: `void uart_putu(unsigned int u)`  
**Parameters**:
- `u` - Unsigned integer to display

**Algorithm**:
1. Special case: if `u == 0`, output '0' and return
2. Extract decimal digits using modulo-10, store in buffer
3. Reverse buffer (digits extracted least-significant first)
4. Output each digit as ASCII character

#### `void uart_put_hex(unsigned int v)`
**Purpose**: Output unsigned integer in hexadecimal format (8 digits)  
**Signature**: `void uart_put_hex(unsigned int v)`  
**Parameters**:
- `v` - Value to display as hex

**Behavior**:
- Always outputs exactly 8 hex digits (zero-padded)
- Uppercase hexadecimal (0-9, A-F)
- Used for debugging memory addresses and register values

**Implementation**:
```c
void uart_put_hex(unsigned int v) {
    char buf[9];
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; ++i) {
        int shift = (7 - i) * 4;
        unsigned int nib = (v >> shift) & 0xF;
        buf[i] = hex[nib];
    }
    buf[8] = '\0';
    uart_puts(buf);
}
```

### System Functions

#### `void panic(const char *reason)`
**Purpose**: Fatal error handler - display message and halt system  
**Signature**: `void panic(const char *reason)`  
**Parameters**:
- `reason` - Error message describing panic condition

**Behavior**:
1. Output "[PANIC] " prefix
2. Output reason string
3. Output "System halted." message
4. Enter infinite loop (system halt)

**Usage**: Called when kernel encounters unrecoverable error

## Implementation Details

### Polling vs. Interrupt-Driven

The UART driver uses **polling** rather than interrupts for transmission and reception:

**Transmission**:
- `uart_putc()` busy-waits on TXFF flag
- Simple and reliable for kernel output
- Acceptable latency for low-volume debug output

**Reception**:
- `uart_getc()` polls RXFE flag with scheduler yield
- Allows other tasks to run while waiting
- `uart_haschar()` provides non-blocking check for IRQ subsystem

### Scheduler Integration

When compiled with scheduler support (default), `uart_getc()` yields CPU while waiting:

```c
while (mmio_read(UART_FR) & (1 << 4)) {
#ifndef NO_SCHED
    extern void yield(void);
    yield();
#endif
}
```

This prevents blocking the entire system when waiting for UART input. The `NO_SCHED` define allows building without scheduler dependency for early boot code.

### IRQ Integration

The IRQ subsystem calls `uart_haschar()` in its polling loop:

```c
void irq_poll_and_dispatch(void) {
    if (uart_haschar()) {
        irq_dispatch(1);  // Dispatch UART IRQ (IRQ 1)
    }
    // ... other polling ...
}
```

This allows UART input to trigger registered interrupt handlers without true hardware interrupt configuration.

## Design Decisions

### Why Polling Instead of Interrupts?

1. **Simplicity**: No interrupt handler management, no FIFO overflow handling
2. **Reliability**: QEMU virt machine UART works reliably with polling
3. **Low Volume**: Serial console has low data rate, polling overhead negligible
4. **Early Boot**: Works before interrupt subsystem initialized

### Why Memory-Mapped I/O?

ARM PL011 UART is a memory-mapped peripheral - registers appear at fixed memory addresses. The `volatile` qualifier ensures:
- Compiler doesn't cache register reads
- Compiler doesn't optimize away register writes
- Every access hits hardware

### Why 8-bit Masking in uart_getc()?

The PL011 UART_DR register is 32 bits but only bits [7:0] contain character data. Higher bits contain error flags. Masking with `& 0xFF` ensures clean character data and ignores error bits.

## Constraints

### Hardware Constraints

- **Fixed Base Address**: 0x09000000 (QEMU virt machine specific)
- **No Baud Rate Configuration**: Assumes QEMU default configuration
- **No FIFO Control**: Uses default FIFO settings
- **No Error Handling**: Ignores framing/parity/overrun errors

### Performance Constraints

- **Blocking I/O**: `uart_putc()` busy-waits, can delay system
- **No Buffering**: Each character requires hardware access
- **Single-Threaded**: No mutual exclusion (assumes single-core or careful usage)

### Portability Constraints

- **Platform-Specific**: Hardcoded for QEMU virt machine
- **PL011-Specific**: Won't work with other UART models
- **Address Assumptions**: Requires identity-mapped memory at 0x09000000

## Usage Examples

### Basic Output

```c
#include "uart.h"

void example_output(void) {
    uart_puts("Hello, MYRASPOS!\n");
    uart_puts("Value: ");
    uart_putu(12345);
    uart_puts("\n");
}
```

### Debugging with Hex Output

```c
void debug_registers(void) {
    uart_puts("Register dump:\n");
    uart_puts("  PC: 0x");
    uart_put_hex(get_pc());
    uart_puts("\n");
}
```

### Input Loop

```c
void read_command(void) {
    uart_puts("Enter command: ");
    char cmd[64];
    int i = 0;
    while (i < 63) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') break;
        cmd[i++] = c;
        uart_putc(c);  // Echo
    }
    cmd[i] = '\0';
}
```

### Panic Handling

```c
void validate_pointer(void *ptr) {
    if (ptr == NULL) {
        panic("Null pointer dereference");
    }
}
```

### Non-Blocking Input Check

```c
void poll_uart(void) {
    if (uart_haschar()) {
        char c = uart_getc();
        handle_input(c);
    }
}
```

## Cross-References

### Related Documentation
- [irq.c.md](irq.c.md) - Interrupt controller that polls UART
- [timer.c.md](timer.c.md) - Timer used with yield in uart_getc()

### Related Source Files
- `kernel/uart.c` - Implementation
- `kernel/uart.h` - Public API
- `kernel/irq.c` - Uses uart_haschar() for polling
- `kernel/panic.c` - May use UART for panic output

### Data Flow
```
User Code
    ↓
uart_puts() / uart_putc()
    ↓
MMIO Write to 0x09000000 (UART_DR)
    ↓
PL011 UART Hardware
    ↓
Serial Port Output
```

```
Serial Port Input
    ↓
PL011 UART Hardware
    ↓
irq_poll_and_dispatch() checks uart_haschar()
    ↓
MMIO Read from 0x09000000 (UART_DR)
    ↓
uart_getc()
    ↓
User Code
```

### Key Dependencies
- **Hardware**: PL011 UART at 0x09000000
- **Scheduler**: `yield()` function (optional, via `NO_SCHED`)
- **IRQ System**: Polling integration
- **Standard Types**: `stdint.h` for fixed-width types

## Register Reference

### Complete PL011 Register Map (Reference)

While the driver only uses DR and FR, the complete PL011 register set includes:

| Offset | Register | Full Name |
|--------|----------|-----------|
| 0x000 | UARTDR | Data Register |
| 0x004 | UARTRSR/UARTECR | Receive Status/Error Clear |
| 0x018 | UARTFR | Flag Register |
| 0x020 | UARTILPR | IrDA Low-Power Counter |
| 0x024 | UARTIBRD | Integer Baud Rate Divisor |
| 0x028 | UARTFBRD | Fractional Baud Rate Divisor |
| 0x02C | UARTLCR_H | Line Control Register |
| 0x030 | UARTCR | Control Register |
| 0x034 | UARTIFLS | Interrupt FIFO Level Select |
| 0x038 | UARTIMSC | Interrupt Mask Set/Clear |
| 0x03C | UARTRIS | Raw Interrupt Status |
| 0x040 | UARTMIS | Masked Interrupt Status |
| 0x044 | UARTICR | Interrupt Clear Register |
| 0x048 | UARTDMACR | DMA Control Register |

**Note**: MYRASPOS driver uses minimal subset (DR, FR) for simplicity.

## Thread Safety

The UART driver is **not thread-safe**:
- No mutual exclusion around MMIO accesses
- Assumes single-core execution or serialized access
- Multiple concurrent callers can interleave output characters

For multi-threaded kernels, wrap UART calls with locks:

```c
void safe_uart_puts(const char *s) {
    unsigned long flags = irq_save();
    uart_puts(s);
    irq_restore(flags);
}
```

## Future Enhancements

Possible improvements:
1. **Interrupt-Driven I/O**: Use RX/TX interrupts instead of polling
2. **Buffered Output**: Ring buffer for async transmission
3. **Error Handling**: Check and report framing/parity errors
4. **Baud Rate Configuration**: Runtime baud rate setting
5. **Thread Safety**: Add spinlock protection
6. **Platform Abstraction**: Device tree or probe-based initialization
