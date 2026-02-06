# Interrupt Controller Documentation (irq.c/h)

## Overview

The IRQ (Interrupt Request) subsystem manages hardware interrupts for MYRASPOS using the ARM Generic Interrupt Controller (GIC). It provides interrupt handler registration, dispatch, and masking capabilities for both timer interrupts (Private Peripheral Interrupts - PPIs) and device interrupts (Shared Peripheral Interrupts - SPIs).

**Hardware Interface**: ARM GIC (Generic Interrupt Controller)  
**GIC Distributor Base**: `0x08000000`  
**GIC CPU Interface Base**: `0x08010000`  
**Architecture**: GICv2 compatible

## Hardware Details

### GIC Architecture

The ARM GIC has two main components:

1. **Distributor (GICD)**: Manages interrupt sources, routing, priorities
2. **CPU Interface (GICC)**: Per-CPU interface for interrupt acknowledgment

### Interrupt Types

| Type | ID Range | Description |
|------|----------|-------------|
| SGI | 0-15 | Software Generated Interrupts (inter-processor) |
| PPI | 16-31 | Private Peripheral Interrupts (per-CPU, e.g., timer) |
| SPI | 32-1019 | Shared Peripheral Interrupts (devices) |

### GIC Distributor Registers (GICD)

**Base Address**: `0x08000000`

| Register | Offset | Description |
|----------|--------|-------------|
| GICD_CTLR | 0x000 | Distributor Control Register |
| GICD_ISENABLER | 0x100 | Interrupt Set-Enable Registers |
| GICD_ICENABLER | 0x180 | Interrupt Clear-Enable Registers |
| GICD_ITARGETSR | 0x800 | Interrupt Processor Targets Registers |
| GICD_IGROUPR | 0x080 | Interrupt Group Registers |

**GICD_CTLR**: Bit 0 enables the distributor  
**GICD_ISENABLER**: 32 registers, 1 bit per interrupt (set bit to enable)  
**GICD_ICENABLER**: 32 registers, 1 bit per interrupt (set bit to disable)  
**GICD_ITARGETSR**: Byte per interrupt, specifies target CPU(s)  
**GICD_IGROUPR**: 1 bit per interrupt, 0=Group 0 (FIQ), 1=Group 1 (IRQ)

### GIC CPU Interface Registers (GICC)

**Base Address**: `0x08010000`

| Register | Offset | Description |
|----------|--------|-------------|
| GICC_CTLR | 0x00 | CPU Interface Control Register |
| GICC_PMR | 0x04 | Interrupt Priority Mask Register |
| GICC_IAR | 0x0C | Interrupt Acknowledge Register |
| GICC_EOIR | 0x10 | End Of Interrupt Register |

**GICC_CTLR**: Bits [1:0] enable Group 0 and Group 1 interrupts  
**GICC_PMR**: Priority mask (0xFF = allow all priorities)  
**GICC_IAR**: Read to acknowledge interrupt, returns interrupt ID  
**GICC_EOIR**: Write to signal interrupt completion

## Key Data Structures

### Interrupt Handler Entry

```c
struct irq_entry {
    int num;              // IRQ number
    irq_handler_fn fn;    // Handler function pointer
    void *arg;            // Argument passed to handler
};
```

**Handler Array**: `static struct irq_entry handlers[MAX_IRQ_HANDLERS]`  
**Capacity**: 16 handlers (MAX_IRQ_HANDLERS)

### Processor State Structure

```c
struct pt_regs {
    uint64_t regs[30];    // x0-x29 general purpose registers
    uint64_t lr;          // x30 link register
    uint64_t elr;         // Exception link register (return address)
    uint64_t spsr;        // Saved program status register
};
```

**Purpose**: Captures CPU state at interrupt entry (from assembly)  
**Layout**: Matches stack layout created by `vectors.S` kernel_entry

### Handler Function Type

```c
typedef void (*irq_handler_fn)(void *arg);
```

Handler signature: `void handler(void *arg)`

## Key Functions

### Initialization

#### `void irq_init(void)`
**Purpose**: Initialize GIC and vector table  
**Signature**: `void irq_init(void)`  
**Parameters**: None

**Algorithm**:
1. Set VBAR_EL1 (Vector Base Address Register) to assembly vector table
2. Clear all handler slots
3. Configure GIC CPU Interface (enable groups, set priority mask)
4. Enable GIC Distributor
5. Mask all SPIs (32-1019) to prevent spurious interrupts

**Implementation Details**:
```c
void irq_init(void) {
    /* Setup Vector Base Address Register */
    uintptr_t v = (uintptr_t)vectors;
    __asm__ volatile("msr vbar_el1, %0" : : "r"(v));

    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) 
        handlers[i].fn = NULL;
    
    /* Enable GIC CPU Interface */
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(GICC_BASE + GICC_CTLR);
    volatile uint32_t *gicc_pmr = (volatile uint32_t *)(GICC_BASE + GICC_PMR);
    *gicc_pmr = 0xFF;     // Allow all priorities
    *gicc_ctlr = 0x3;     // Enable Group 0 and Group 1

    /* Enable GIC Distributor */
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(GICD_BASE + GICD_CTLR);
    *gicd_ctlr = 0x1;

    /* Mask all SPIs to prevent storms */
    volatile uint32_t *icenable = (volatile uint32_t *)(GICD_BASE + GICD_ICENABLER);
    for (int i = 0; i < 32; ++i) {
        icenable[i] = 0xFFFFFFFF;  // Disable all
    }
}
```

**Why Mask SPIs?**: Unhandled SPIs can cause interrupt storms. Only unmask specific IRQs via registration.

### Interrupt Registration

#### `int irq_register(int irq_num, irq_handler_fn fn, void *arg)`
**Purpose**: Register handler for specific interrupt  
**Signature**: `int irq_register(int irq_num, irq_handler_fn fn, void *arg)`  
**Parameters**:
- `irq_num` - Interrupt number (0-1019)
- `fn` - Handler function pointer
- `arg` - Argument to pass to handler

**Returns**:
- `0` on success
- `-1` if handler table full

**Behavior**:
1. Find empty slot in handler array
2. Store IRQ number, function pointer, and argument
3. Call `irq_unmask()` to enable interrupt at GIC

**Thread Safety**: Not thread-safe, call during initialization only

#### `void irq_unmask(int irq_num)` (Internal)
**Purpose**: Unmask interrupt at GIC distributor  
**Signature**: `void irq_unmask(int irq_num)`  
**Parameters**:
- `irq_num` - Interrupt number to enable

**Algorithm**:
1. Configure GICD_IGROUPR (set to Group 0)
2. Set bit in GICD_ISENABLER register
3. For SPIs (≥32), configure GICD_ITARGETSR to target CPU 0

**Implementation**:
```c
void irq_unmask(int irq_num) {
    /* Set to Group 0 */
    volatile uint32_t *igroupr = (volatile uint32_t *)(GICD_BASE + 0x080);
    igroupr[irq_num / 32] &= ~(1 << (irq_num % 32));

    /* Enable interrupt */
    volatile uint32_t *isenable = (volatile uint32_t *)(GICD_BASE + GICD_ISENABLER);
    isenable[irq_num / 32] = (1 << (irq_num % 32));
    
    /* Target CPU 0 for SPIs */
    if (irq_num >= 32) {
        volatile uint8_t *itargetset = (volatile uint8_t *)(GICD_BASE + GICD_ITARGETSR);
        itargetset[irq_num] = 0x01;  // CPU 0
    }
}
```

### Interrupt Dispatch

#### `void irq_dispatch(int irq_num)`
**Purpose**: Call all handlers registered for specific IRQ  
**Signature**: `void irq_dispatch(int irq_num)`  
**Parameters**:
- `irq_num` - Interrupt number to dispatch

**Behavior**:
- Iterates through handler table
- Calls every handler where `handlers[i].num == irq_num`
- Passes registered `arg` to each handler

**Design**: Allows multiple handlers per IRQ (broadcast)

#### `void irq_entry_c(void)`
**Purpose**: C entry point from assembly IRQ vector  
**Signature**: `void irq_entry_c(void)`  
**Called From**: `vectors.S` IRQ handler

**Algorithm**:
1. Read GICC_IAR to acknowledge interrupt and get IRQ number
2. If valid IRQ (< 1022):
   - Call `irq_dispatch(irq_num)` to invoke handlers
   - Write GICC_EOIR to signal interrupt completion
3. Else (spurious or special):
   - Advance scheduler tick
   - Call `irq_poll_and_dispatch()` for software polling
4. Request scheduler preemption

**Implementation**:
```c
void irq_entry_c(void) {
    volatile uint32_t *gicc_iar = (volatile uint32_t *)(GICC_BASE + GICC_IAR);
    volatile uint32_t *gicc_eoir = (volatile uint32_t *)(GICC_BASE + GICC_EOIR);
    
    uint32_t iar = *gicc_iar;
    uint32_t irq_num = iar & 0x3FF;  // Extract IRQ ID [9:0]
    
    if (irq_num < 1022) {
        irq_dispatch((int)irq_num);
        *gicc_eoir = iar;  // End interrupt
    } else {
        /* Fallback for timer tick */
        scheduler_tick_advance(1);
        irq_poll_and_dispatch();
    }
    
    scheduler_request_preempt();
}
```

**Special IRQ Numbers**:
- `1022`: Reserved
- `1023`: Spurious interrupt

### Polling Support

#### `void irq_poll_and_dispatch(void)`
**Purpose**: Poll devices and dispatch handlers (software interrupts)  
**Signature**: `void irq_poll_and_dispatch(void)`  
**Called From**: Scheduler loop, `irq_entry_c()` fallback

**Behavior**:
1. Check if UART has data via `uart_haschar()`
2. If yes, dispatch IRQ 1 (UART pseudo-IRQ)
3. Call `virtio_input_poll()` for VirtIO devices

**Design Rationale**: Hybrid interrupt model - hardware timer interrupts trigger polling of other devices

### Critical Sections

#### `unsigned long irq_save(void)` (Inline)
**Purpose**: Disable interrupts and save previous state  
**Signature**: `static inline unsigned long irq_save(void)`  
**Returns**: Previous DAIF register value (for restore)

**Implementation**:
```c
static inline unsigned long irq_save(void) {
    unsigned long flags;
    __asm__ volatile(
        "mrs %0, daif\n"      // Read current interrupt state
        "msr daifset, #2"     // Disable IRQs (bit 1 of DAIF)
        : "=r" (flags)
        :
        : "memory"
    );
    return flags;
}
```

**DAIF Register Bits**:
- D: Debug exceptions
- A: SError (System Error)
- I: IRQ
- F: FIQ

#### `void irq_restore(unsigned long flags)` (Inline)
**Purpose**: Restore previous interrupt state  
**Signature**: `static inline void irq_restore(unsigned long flags)`  
**Parameters**:
- `flags` - Value returned by `irq_save()`

**Implementation**:
```c
static inline void irq_restore(unsigned long flags) {
    __asm__ volatile(
        "msr daif, %0"
        :
        : "r" (flags)
        : "memory"
    );
}
```

**Usage Pattern**:
```c
unsigned long flags = irq_save();
/* Critical section */
irq_restore(flags);
```

## Implementation Details

### Vector Table Setup

The `vectors` symbol points to assembly exception handlers in `vectors.S`. Setting VBAR_EL1 tells the CPU where to find interrupt handlers:

```
VBAR_EL1 + 0x000: Synchronous exceptions
VBAR_EL1 + 0x080: IRQ interrupts
VBAR_EL1 + 0x100: FIQ interrupts
VBAR_EL1 + 0x180: SError interrupts
```

Each vector saves registers to stack, calls C handler, restores registers.

### Interrupt Flow

```
Hardware Interrupt
    ↓
CPU jumps to VBAR_EL1 + 0x080
    ↓
Assembly saves registers (vectors.S)
    ↓
irq_entry_c()
    ↓
Read GICC_IAR (acknowledge)
    ↓
irq_dispatch(irq_num)
    ↓
Invoke registered handlers
    ↓
Write GICC_EOIR (end interrupt)
    ↓
scheduler_request_preempt()
    ↓
Assembly restores registers
    ↓
Return to interrupted code
```

### Hybrid Interrupt Model

MYRASPOS uses a hybrid approach:

**Hardware Interrupts**: Timer (PPI) generates actual GIC interrupts  
**Software Polling**: UART and VirtIO devices polled in interrupt handler

**Advantages**:
- Simpler driver code (no interrupt setup per device)
- Reliable on QEMU (avoids interrupt routing issues)
- Low latency for timer, acceptable for low-rate devices

**Trade-offs**:
- UART/VirtIO latency depends on timer tick rate
- Polling overhead in every timer interrupt

### SPI Masking Strategy

The initialization masks all SPIs (32-1019) because:

1. **Unknown Devices**: QEMU virt machine has many potential devices
2. **Interrupt Storms**: Unhandled interrupts cause kernel hang
3. **Selective Enable**: Only registered interrupts are unmasked

This "secure by default" approach prevents spurious interrupts from unknown devices.

## Design Decisions

### Why GICv2 Instead of GICv3?

QEMU virt machine defaults to GICv2. GICv3 provides more features (affinity routing, more priority levels) but adds complexity. GICv2 sufficient for single-core embedded OS.

### Why Store IRQ Number in Handler Entry?

Allows multiple handlers per IRQ number and simplifies dispatch logic. Alternative would be array indexed by IRQ number, but wastes memory for sparse interrupt maps.

### Why Fallback to Polling in irq_entry_c()?

When IAR returns ≥1022 (spurious/special), still need to:
- Advance scheduler time
- Poll devices that rely on timer-based polling

This ensures system progresses even without valid hardware IRQ.

### Why Inline irq_save/restore?

Critical sections used frequently. Inlining:
- Eliminates function call overhead
- Compiler can optimize around inline assembly
- Visible to compiler for analysis

### Why Volatile Pointers for MMIO?

GIC registers are memory-mapped hardware:
```c
volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(GICC_BASE + GICC_CTLR);
```

`volatile` prevents compiler from:
- Caching register reads
- Eliminating "redundant" writes
- Reordering memory accesses

## Constraints

### Hardware Constraints

- **Platform-Specific**: Hardcoded for QEMU virt machine GIC addresses
- **GICv2 Only**: Not compatible with GICv3 or other interrupt controllers
- **Single Core**: Assumes CPU 0, no SMP support
- **Limited SPIs**: Only handles IRQ 0-1019 (standard GIC range)

### Software Constraints

- **Handler Limit**: Maximum 16 registered handlers (MAX_IRQ_HANDLERS)
- **No Priorities**: All interrupts same priority (0xFF)
- **No Nested Interrupts**: Interrupts disabled during handler execution
- **No SMP**: No inter-processor interrupts (IPIs)

### Design Constraints

- **Polling Dependency**: UART/VirtIO require timer interrupts for polling
- **Single Handler**: Only one handler function per registration (but can register multiple times)
- **Static Registration**: Must register during init, no runtime add/remove

## Usage Examples

### Register Timer Handler

```c
void timer_handler(void *arg) {
    // Handle timer interrupt
    scheduler_tick_advance(1);
}

void setup_timer(void) {
    irq_register(30, timer_handler, NULL);  // IRQ 30 = generic timer
}
```

### Register Device Handler

```c
void uart_handler(void *arg) {
    while (uart_haschar()) {
        char c = uart_getc();
        process_input(c);
    }
}

void setup_uart(void) {
    irq_register(1, uart_handler, NULL);  // Pseudo-IRQ 1 for UART
}
```

### Critical Section Protection

```c
void modify_shared_data(void) {
    unsigned long flags = irq_save();
    
    // Interrupts disabled here
    shared_counter++;
    update_structure();
    
    irq_restore(flags);
}
```

### Polling Integration

```c
void scheduler_loop(void) {
    while (1) {
        timer_poll_and_advance();
        irq_poll_and_dispatch();  // Poll UART, VirtIO
        task_schedule();
    }
}
```

## Cross-References

### Related Documentation
- [uart.c.md](uart.c.md) - UART driver polled by IRQ system
- [timer.c.md](timer.c.md) - Timer generates interrupts
- [input.c.md](input.c.md) - Uses irq_save/restore for thread safety

### Related Source Files
- `kernel/irq.c` - Implementation
- `kernel/irq.h` - Public API
- `kernel/vectors.S` - Assembly exception handlers
- `kernel/sched.c` - Scheduler integration
- `kernel/uart.c` - Polled by IRQ system
- `kernel/virtio.c` - Polled by IRQ system

### Assembly Integration

**vectors.S** provides assembly entry points:
```asm
.globl vectors
vectors:
    // Synchronous exceptions at +0x000
    // IRQ at +0x080
    // FIQ at +0x100
    // SError at +0x180
```

Each vector:
1. Saves registers to stack (creates pt_regs structure)
2. Calls C handler (irq_entry_c)
3. Restores registers from stack
4. Returns with ERET instruction

### Scheduler Integration

The IRQ system integrates with scheduler:
- **scheduler_tick_advance()**: Updates time slice counters
- **scheduler_request_preempt()**: Marks preemption needed
- **task_schedule()**: Context switches if preemption requested

## Thread Safety

### IRQ Handler Context

Handlers execute with interrupts disabled:
- No nested interrupts
- No preemption during handler
- Must complete quickly to avoid latency

### Shared Data Protection

Use `irq_save()`/`irq_restore()` to protect:
- Global variables accessed by both IRQ handlers and normal code
- Data structures modified by multiple handlers
- Hardware registers requiring atomic sequences

### Handler Registration

`irq_register()` is **not thread-safe**:
- Call during single-threaded initialization only
- No locking around handler array access

## Performance Considerations

### Interrupt Latency

From hardware IRQ to C handler:
1. CPU exception entry (hardware): ~10 cycles
2. Register save (assembly): ~60 cycles (30 registers)
3. C function call: ~5 cycles
4. **Total**: ~75 cycles + handler execution

### Polling Overhead

Every timer interrupt polls UART and VirtIO:
- UART check: 1 MMIO read (UART_FR)
- VirtIO poll: Device-dependent
- Overhead: ~100-500 cycles per timer tick

### Dispatch Performance

Handler lookup is O(n) where n = registered handlers:
- Best case: O(1) if first handler matches
- Worst case: O(16) (MAX_IRQ_HANDLERS)
- Typical: O(1-3) for small handler count

Could optimize with hash table or IRQ-indexed array.

## Debugging

### Enable IRQ Debug Output

Uncomment debug line in `irq_entry_c()`:
```c
// uart_puts("[irq] handler for "); uart_put_hex(irq_num); uart_puts("\n");
```

Shows each interrupt number received.

### Check GIC State

```c
void dump_gic_state(void) {
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(GICD_BASE + GICD_CTLR);
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(GICC_BASE + GICC_CTLR);
    
    uart_puts("GICD_CTLR: 0x"); uart_put_hex(*gicd_ctlr); uart_puts("\n");
    uart_puts("GICC_CTLR: 0x"); uart_put_hex(*gicc_ctlr); uart_puts("\n");
}
```

### Verify VBAR

```c
void check_vbar(void) {
    uint64_t vbar;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    uart_puts("VBAR_EL1: 0x"); uart_put_hex((uint32_t)vbar); uart_puts("\n");
}
```

## Future Enhancements

Possible improvements:
1. **Dynamic Registration**: Add/remove handlers at runtime
2. **Priority Levels**: Use GIC priority registers
3. **Nested Interrupts**: Allow higher-priority interrupts
4. **SMP Support**: Multi-core interrupt affinity
5. **True IRQ Mode**: Enable actual UART/VirtIO interrupts
6. **Performance**: Hash table or array-based dispatch
7. **Statistics**: Track interrupt counts per IRQ
8. **Soft IRQs**: Deferred interrupt processing
