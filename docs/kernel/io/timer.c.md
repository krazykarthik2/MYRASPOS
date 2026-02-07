# ARM Generic Timer Documentation (timer.c/h)

## Overview

The timer subsystem provides monotonic time tracking and task sleeping capabilities for MYRASPOS. It uses the ARM Generic Timer (architected timer) available on all ARMv8 processors to maintain system time and enable time-based scheduling.

**Hardware Interface**: ARM Generic Timer (System Register Interface)  
**Timer Type**: Physical Counter (CNTPCT_EL0)  
**Resolution**: 64-bit counter at configurable frequency  
**Typical Frequency**: 62.5 MHz (QEMU default)

## Hardware Details

### ARM Generic Timer Architecture

The ARM Generic Timer provides:
- **CNTPCT_EL0**: 64-bit physical count register (read-only)
- **CNTFRQ_EL0**: Counter frequency register (Hz)
- **CNTP_CTL_EL0**: Timer control register
- **CNTP_CVAL_EL0**: Timer comparator value
- **CNTP_TVAL_EL0**: Timer value register

### System Registers

| Register | Access | Description |
|----------|--------|-------------|
| CNTPCT_EL0 | Read | Current counter value (64-bit) |
| CNTFRQ_EL0 | Read | Counter frequency in Hz |
| CNTP_CTL_EL0 | R/W | Timer control (enable, mask, status) |
| CNTP_CVAL_EL0 | R/W | Comparator value for timer interrupt |
| CNTP_TVAL_EL0 | R/W | Timer value (countdown) |

### Counter Properties

**Monotonic**: Counter only increments, never decreases or wraps (64-bit)  
**Frequency**: Fixed at boot, typically 62.5 MHz on QEMU  
**Resolution**: 1/frequency seconds per tick (~16 ns at 62.5 MHz)  
**Range**: 64-bit counter provides ~9300 years at 62.5 MHz

### Accessing System Registers

ARM system registers accessed via MRS (Move to Register from System) instruction:

```c
uint64_t ticks;
__asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
```

```c
uint64_t freq;
__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
```

## Key Functions

### Initialization

#### `void timer_init(void)`
**Purpose**: Initialize timer subsystem and read hardware frequency  
**Signature**: `void timer_init(void)`  
**Parameters**: None

**Algorithm**:
1. Read CNTFRQ_EL0 to get counter frequency
2. If frequency is 0 (undefined), use fallback: 62500000 Hz (62.5 MHz)
3. Read current CNTPCT_EL0 value
4. Convert to milliseconds and store as initial time

**Implementation**:
```c
void timer_init(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_freq));
    if (counter_freq == 0) counter_freq = 62500000;  // Fallback
    
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    last_ms = (uint32_t)(ticks * 1000 / counter_freq);
}
```

**Why Fallback Frequency?**: Some platforms may not initialize CNTFRQ_EL0. 62.5 MHz is QEMU virt machine default.

**Why 32-bit Milliseconds?**: Simplifies time tracking, provides ~49 day range before wrap.

### Time Retrieval

#### `uint32_t timer_get_ms(void)`
**Purpose**: Get current time in milliseconds since initialization  
**Signature**: `uint32_t timer_get_ms(void)`  
**Returns**: Milliseconds elapsed (32-bit, wraps at ~49 days)

**Algorithm**:
1. Read CNTPCT_EL0 (current counter value)
2. Convert to milliseconds: `(ticks * 1000) / counter_freq`
3. Return as 32-bit unsigned integer

**Implementation**:
```c
uint32_t timer_get_ms(void) {
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return (uint32_t)(ticks * 1000 / counter_freq);
}
```

**Precision**: Depends on counter frequency:
- 62.5 MHz: 0.016 ms resolution (~60 Hz granularity)
- Formula: resolution = 1000 / freq seconds

**Wrap Behavior**: 32-bit millisecond counter wraps at 2^32 ms ≈ 49.7 days

### Task Sleeping

#### `void timer_sleep_ms(uint32_t ms)`
**Purpose**: Block current task for specified milliseconds  
**Signature**: `void timer_sleep_ms(uint32_t ms)`  
**Parameters**:
- `ms` - Milliseconds to sleep

**Behavior**:
1. Calculate wake time: `current_time + ms`
2. Call `task_block_current_until(wake)` to block task
3. Scheduler wakes task when system time ≥ wake time

**Implementation**:
```c
void timer_sleep_ms(uint32_t ms) {
    uint32_t now = timer_get_ms();
    uint32_t wake = now + ms;
    task_block_current_until(wake);
}
```

**Scheduler Integration**: Task added to sleep queue, removed when wake time reached.

**Wrap Handling**: Scheduler must handle wake time wrap at 2^32 ms.

### Scheduler Integration

#### `void timer_poll_and_advance(void)`
**Purpose**: Update scheduler tick count based on elapsed time  
**Signature**: `void timer_poll_and_advance(void)`  
**Called From**: Scheduler main loop

**Algorithm**:
1. Read current time via `timer_get_ms()`
2. Calculate elapsed: `current - last_ms`
3. If elapsed > 0:
   - Call `scheduler_tick_advance(elapsed)`
   - Update `last_ms` to current time

**Implementation**:
```c
void timer_poll_and_advance(void) {
    uint32_t now = timer_get_ms();
    if (now > last_ms) {
        scheduler_tick_advance(now - last_ms);
        last_ms = now;
    }
}
```

**Purpose**: Converts hardware time to scheduler ticks (1 tick = 1 ms).

**Why Polling?**: Software-based timing avoids configuring timer interrupts, simpler for scheduler-driven design.

## Implementation Details

### Software Monotonic Fallback

Despite being titled "software-monotonic fallback," this implementation actually uses the hardware counter (CNTPCT_EL0). The "fallback" refers to:

1. **No Timer Interrupts**: Doesn't configure CNTP_CVAL/TVAL for hardware interrupts
2. **Polling-Based**: Scheduler polls timer instead of interrupt-driven
3. **Simple Conversion**: Direct tick-to-millisecond conversion

**Original Design Intent**: Avoid MMIO accesses that could cause data aborts on some QEMU configurations. System registers (MRS) are safer than MMIO.

### Time Conversion Formula

**Ticks to Milliseconds**:
```
ms = (ticks * 1000) / counter_freq
```

**Example** (62.5 MHz):
```
ticks = 625000000 (10 seconds @ 62.5 MHz)
ms = (625000000 * 1000) / 62500000 = 10000 ms
```

**Overflow Considerations**:
- `ticks * 1000` can overflow for large tick values
- 64-bit arithmetic required: `(uint64_t)ticks * 1000`
- Result cast to 32-bit for millisecond storage

### Tick Advance Model

The scheduler uses **tick advance** rather than absolute time:

**Traditional Model**: Scheduler checks current time each iteration  
**Tick Advance Model**: Timer notifies scheduler of elapsed time

**Advantages**:
- Scheduler doesn't need timer API dependency
- Tick granularity independent of timer granularity
- Simpler to test (can advance ticks manually)

**Flow**:
```
timer_poll_and_advance()
    ↓
Calculate elapsed milliseconds
    ↓
scheduler_tick_advance(elapsed)
    ↓
Update time slices
Wake sleeping tasks
```

### Frequency Detection

The `timer_init()` reads CNTFRQ_EL0:

**Bootloader Sets**: ARM spec requires bootloader to set CNTFRQ_EL0  
**Fallback**: If 0 (not set), use 62.5 MHz (QEMU default)  
**Platform Variance**: Real hardware may use different frequencies (e.g., 19.2 MHz, 24 MHz, 1 GHz)

**Detection Code**:
```c
__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_freq));
if (counter_freq == 0) counter_freq = 62500000;
```

## Design Decisions

### Why Millisecond Resolution?

**Sufficient Granularity**: 1 ms adequate for task scheduling (typical time slices 10-100 ms)  
**Simple Arithmetic**: Integer milliseconds avoid floating point  
**Range**: 32-bit provides 49.7 days, acceptable for embedded systems  
**Compatibility**: Standard time unit, easy to understand

### Why Polling Instead of Interrupts?

**Simplicity**: No interrupt handler, no comparator configuration  
**Scheduler Control**: Scheduler decides when to update time  
**Flexibility**: Works with cooperative or preemptive scheduling  
**Reliability**: Avoids timer interrupt setup issues on QEMU

**Trade-off**: Requires regular polling, adds scheduler loop overhead

### Why System Registers Instead of MMIO?

ARM Generic Timer accessible two ways:
1. **System Registers** (MRS/MSR instructions)
2. **Memory-Mapped** (MMIO at platform-specific address)

**System Register Advantages**:
- No platform-specific addresses
- CPU instructions, not memory bus
- Faster access
- No potential for data aborts
- Portable across ARM platforms

### Why 64-bit Ticks but 32-bit Milliseconds?

**64-bit Counter**: Hardware provides 64-bit for long uptime  
**32-bit Milliseconds**: Sufficient for application needs, saves memory

**Conversion**: Read 64-bit, convert to 32-bit ms

**Wrap Handling**: Application must handle 49-day wrap if needed (rare for embedded)

### Why Software "Last MS" Tracking?

`last_ms` variable tracks last scheduler update time:

**Purpose**: Detect elapsed time between polls  
**Efficiency**: Avoids recalculating elapsed time every poll  
**Accuracy**: Ensures scheduler advances by exact elapsed time

**Without last_ms**: Would need separate start time and current time comparison

## Constraints

### Hardware Constraints

- **ARM Architecture**: Requires ARMv8 generic timer
- **EL1 Access**: Must run at EL1 (kernel privilege) to access system registers
- **Frequency Dependency**: Relies on CNTFRQ_EL0 or fallback
- **64-bit Counter**: Assumes counter doesn't overflow (practically never wraps)

### Software Constraints

- **32-bit Milliseconds**: Wraps at 2^32 ms (~49.7 days)
- **1 ms Granularity**: Can't schedule finer than 1 ms
- **Polling Required**: Must call `timer_poll_and_advance()` regularly
- **Single Timer**: No support for multiple timers or alarms

### Performance Constraints

- **Division Overhead**: `ticks * 1000 / counter_freq` requires 64-bit division
- **Poll Frequency**: Timer accuracy depends on poll rate
- **No Callbacks**: No timer interrupt callbacks, must poll

## Usage Examples

### Basic Time Retrieval

```c
#include "timer.h"

void log_event(void) {
    uint32_t now = timer_get_ms();
    uart_puts("Event at ");
    uart_putu(now);
    uart_puts(" ms\n");
}
```

### Task Sleep

```c
void blink_led(void) {
    while (1) {
        led_on();
        timer_sleep_ms(500);  // 500 ms on
        
        led_off();
        timer_sleep_ms(500);  // 500 ms off
    }
}
```

### Timeout Implementation

```c
int wait_for_condition(void) {
    uint32_t start = timer_get_ms();
    uint32_t timeout = 5000;  // 5 seconds
    
    while (!condition_met()) {
        uint32_t elapsed = timer_get_ms() - start;
        if (elapsed >= timeout) {
            return -1;  // Timeout
        }
        yield();
    }
    return 0;  // Success
}
```

### Measuring Execution Time

```c
void benchmark_function(void) {
    uint32_t start = timer_get_ms();
    
    expensive_operation();
    
    uint32_t end = timer_get_ms();
    uint32_t duration = end - start;
    
    uart_puts("Operation took ");
    uart_putu(duration);
    uart_puts(" ms\n");
}
```

### Scheduler Integration

```c
void scheduler_loop(void) {
    while (1) {
        timer_poll_and_advance();  // Update time
        irq_poll_and_dispatch();   // Handle interrupts
        task_schedule();           // Switch tasks
    }
}
```

### Periodic Task

```c
void periodic_task(void) {
    uint32_t last_run = timer_get_ms();
    uint32_t interval = 100;  // 100 ms
    
    while (1) {
        uint32_t now = timer_get_ms();
        if (now - last_run >= interval) {
            do_periodic_work();
            last_run = now;
        }
        yield();
    }
}
```

### Wrap-Safe Comparison

```c
int is_timeout(uint32_t start_time, uint32_t timeout_ms) {
    uint32_t now = timer_get_ms();
    uint32_t elapsed = now - start_time;  // Works with wrap
    return elapsed >= timeout_ms;
}
```

**Note**: Subtraction handles wrap correctly due to unsigned integer math, as long as timeout < 2^31 ms.

## Cross-References

### Related Documentation
- [irq.c.md](irq.c.md) - IRQ system uses timer for tick advance
- [uart.c.md](uart.c.md) - UART may use timer for timeouts

### Related Source Files
- `kernel/timer.c` - Implementation
- `kernel/timer.h` - Public API
- `kernel/sched.c` - Scheduler uses timer for time slicing
- `kernel/irq.c` - May integrate with timer interrupts

### Scheduler Integration

**Functions Called**:
- `scheduler_tick_advance(uint32_t ticks)` - Advance scheduler time
- `task_block_current_until(uint32_t wake_time)` - Sleep until time

**Data Flow**:
```
Hardware Counter (CNTPCT_EL0)
    ↓
timer_get_ms() reads and converts
    ↓
timer_poll_and_advance() calculates delta
    ↓
scheduler_tick_advance(elapsed_ms)
    ↓
Scheduler updates time slices
    ↓
Wakes sleeping tasks
```

### Time Representation

**Hardware**: 64-bit ticks at counter_freq Hz  
**Timer API**: 32-bit milliseconds  
**Scheduler**: 32-bit ticks (1 tick = 1 ms)  
**Application**: Millisecond precision

## Thread Safety

The timer functions are **mostly thread-safe**:

### Safe Operations
- **timer_get_ms()**: Reads hardware register, atomic
- **timer_sleep_ms()**: Blocks current task, scheduler handles races

### Unsafe Operations
- **timer_init()**: Must be called once during single-threaded boot
- **timer_poll_and_advance()**: Must be called from single context (scheduler loop)

### Race Conditions

**last_ms Variable**: Not protected, assumes single scheduler thread calls `timer_poll_and_advance()`.

**If Multi-Threaded**: Would need lock around:
```c
unsigned long flags = irq_save();
uint32_t now = timer_get_ms();
if (now > last_ms) {
    scheduler_tick_advance(now - last_ms);
    last_ms = now;
}
irq_restore(flags);
```

## Performance Considerations

### Read Overhead

**timer_get_ms()** executes:
1. MRS instruction (read CNTPCT_EL0): ~10 cycles
2. 64-bit multiplication: ~5 cycles
3. 64-bit division: ~20-100 cycles (depending on CPU)

**Total**: ~35-115 cycles per call

**Division Optimization**: Could use fixed-point math or lookup table for common frequencies.

### Polling Overhead

Each scheduler iteration calls `timer_poll_and_advance()`:
- timer_get_ms(): ~35-115 cycles
- Subtraction and comparison: ~5 cycles
- Scheduler update (if changed): Variable

**Frequency**: If scheduler runs 1000 Hz, ~35-115K cycles/sec overhead

**Optimization**: Cache counter_freq division reciprocal

### Sleep Granularity

`timer_sleep_ms()` accuracy depends on:
- Scheduler poll frequency
- Time conversion precision
- Scheduler wake latency

**Example**: 100 Hz scheduler poll → ±10 ms sleep accuracy

## Debugging

### Check Timer Frequency

```c
void debug_timer(void) {
    extern uint64_t counter_freq;
    uart_puts("Counter frequency: ");
    uart_putu((uint32_t)(counter_freq / 1000000));
    uart_puts(" MHz\n");
}
```

### Verify Counter Progress

```c
void test_counter(void) {
    uint64_t t1, t2;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t1));
    for (int i = 0; i < 1000000; i++) __asm__ volatile("nop");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t2));
    
    uart_puts("Counter advanced: ");
    uart_putu((uint32_t)(t2 - t1));
    uart_puts(" ticks\n");
}
```

### Measure Poll Rate

```c
void measure_poll_rate(void) {
    uint32_t start = timer_get_ms();
    uint32_t count = 0;
    
    while (timer_get_ms() - start < 1000) {
        timer_poll_and_advance();
        count++;
    }
    
    uart_puts("Polls per second: ");
    uart_putu(count);
    uart_puts("\n");
}
```

## Future Enhancements

Possible improvements:

1. **Timer Interrupts**: Configure CNTP_CVAL for hardware interrupts instead of polling
2. **64-bit Milliseconds**: Extend range beyond 49 days
3. **Microsecond API**: Higher resolution time for precise measurements
4. **Multiple Timers**: Virtual timer support (multiple alarms)
5. **Frequency Optimization**: Precompute division reciprocal
6. **Drift Correction**: Adjust for accumulated rounding errors
7. **Realtime Clock**: Integrate with RTC for wall clock time
8. **Monotonic Guarantee**: Handle counter frequency changes (CPU throttling)
9. **Per-CPU Timers**: SMP support with per-core time tracking
10. **Timer Callbacks**: Register functions to call at specific times

## ARM Timer Interrupt Configuration (Reference)

For future interrupt-driven implementation:

### Enable Timer Interrupt

```c
void timer_enable_interrupt(uint32_t interval_ms) {
    uint64_t ticks_per_ms = counter_freq / 1000;
    uint64_t interval_ticks = ticks_per_ms * interval_ms;
    
    // Set comparator value
    uint64_t current;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(current));
    uint64_t target = current + interval_ticks;
    __asm__ volatile("msr cntp_cval_el0, %0" : : "r"(target));
    
    // Enable timer
    uint64_t ctl = 1;  // Enable bit
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(ctl));
}
```

### Timer IRQ Handler

```c
void timer_irq_handler(void *arg) {
    // Acknowledge interrupt (read/write CTL)
    uint64_t ctl;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
    
    // Clear ISTATUS bit (bit 2) by writing 0
    ctl &= ~(1UL << 2);
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(ctl));
    
    // Schedule next interrupt
    timer_enable_interrupt(10);  // 10 ms interval
    
    // Advance scheduler
    scheduler_tick_advance(10);
}
```

**Timer IRQ Number**: Typically 30 (Generic Timer Physical PPI on QEMU virt)

**Register with IRQ System**:
```c
irq_register(30, timer_irq_handler, NULL);
```
