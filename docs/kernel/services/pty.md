# Pseudo-Terminal (PTY) Service Documentation

## Overview

The pseudo-terminal service (`pty.c/h`) provides virtual terminal devices for MYRASPOS, enabling:
- Multiple concurrent shell sessions
- Terminal emulation for remote/serial connections
- Bidirectional communication channels between processes
- Line editing and canonical mode input processing
- Buffered I/O for reliable character transmission

PTYs act as intermediaries between terminal clients (e.g., serial connections, network sessions) and shell processes, providing buffering and basic line discipline functionality. Each PTY maintains separate input and output ring buffers with interrupt-safe access.

## Data Structures

### `struct pty`
Core PTY structure providing bidirectional buffered communication.

```c
struct pty {
    char in_buf[512];       // Input buffer (client → shell)
    int in_h, in_t;         // Input buffer head and tail indices
    char out_buf[2048];     // Output buffer (shell → client)
    int out_h, out_t;       // Output buffer head and tail indices
    volatile int lock;      // Spinlock for concurrent access (unused currently)
};
```

**Buffer Architecture:**
- **Input Buffer (512 bytes):** Stores characters from the client/terminal to be read by the shell
- **Output Buffer (2048 bytes):** Stores shell output to be sent to the client/terminal
- **Ring Buffer Implementation:** Circular buffers with head (write) and tail (read) pointers
- **Lock Field:** Reserved for future multi-threading support (currently unused)

**Buffer Full Condition:** `(head + 1) % size == tail`  
**Buffer Empty Condition:** `head == tail`

## Key Functions

### Memory Management

#### `struct pty* pty_alloc(void)`
Allocate and initialize a new PTY structure.

**Behavior:**
1. Allocates PTY structure via `kmalloc()`
2. Zero-initializes all fields (buffers, pointers, lock)
3. Logs error if allocation fails

**Returns:** Pointer to new PTY, or NULL on allocation failure.

**Memory:** Caller is responsible for freeing via `pty_free()`.

**Thread Safety:** Safe to call concurrently (kmalloc is thread-safe).

**Example:**
```c
struct pty *p = pty_alloc();
if (!p) {
    uart_puts("Failed to allocate PTY\n");
    return -1;
}
```

#### `void pty_free(struct pty *p)`
Free a PTY structure.

**Parameters:**
- `p`: PTY to free (NULL-safe)

**Behavior:**
- Calls `kfree(p)` if non-NULL
- Does not flush buffers or notify connected tasks

**Warning:** Ensure no tasks are using the PTY before freeing.

### Input Operations (Client → Shell)

#### `void pty_write_in(struct pty *p, char c)`
Write a character to the input buffer (client writes, shell reads).

**Parameters:**
- `p`: Target PTY
- `c`: Character to write

**Behavior:**
1. Saves interrupt state and disables interrupts
2. Calculates next head position
3. If buffer not full: Writes character and advances head
4. If buffer full: Silently drops character
5. Restores interrupt state

**Thread Safety:** Interrupt-safe via `irq_save()`/`irq_restore()`.

**Use Case:** Terminal client receives character from user, writes to PTY input.

**Example:**
```c
// Serial RX interrupt handler
char c = uart_getc();
pty_write_in(session_pty, c);
```

#### `char pty_read_in(struct pty *p)`
Read a character from the input buffer (shell reads what client wrote).

**Parameters:**
- `p`: Source PTY

**Returns:** 
- Next character from buffer
- `0` if buffer is empty

**Behavior:**
1. Saves interrupt state
2. Checks if buffer empty → returns 0
3. Reads character at tail position
4. Advances tail pointer (circular)
5. Restores interrupt state

**Non-blocking:** Returns immediately even if buffer empty.

**Example:**
```c
// Shell reading user input
char c;
while ((c = pty_read_in(p)) != 0) {
    process_char(c);
}
```

#### `int pty_has_in(struct pty *p)`
Check if input buffer has data available.

**Returns:** 
- Non-zero if data available
- Zero if buffer empty

**Thread Safety:** Interrupt-safe.

**Use Case:** Polling before read to avoid busy-waiting.

### Output Operations (Shell → Client)

#### `void pty_write_out(struct pty *p, char c)`
Write a character to the output buffer (shell writes, client reads).

**Parameters:**
- `p`: Target PTY
- `c`: Character to write

**Behavior:**
1. Saves interrupt state
2. Calculates next head position
3. If buffer not full: Writes character and advances head
4. If buffer full: Silently drops character
5. Restores interrupt state

**Buffer Size:** 2048 bytes (larger than input for output bursts).

**Use Case:** Shell writes command output, client reads and displays.

**Example:**
```c
// Shell writing output
const char *msg = "Hello, PTY!\n";
for (const char *s = msg; *s; ++s) {
    pty_write_out(pty, *s);
}
```

#### `char pty_read_out(struct pty *p)`
Read a character from the output buffer (client reads what shell wrote).

**Parameters:**
- `p`: Source PTY

**Returns:**
- Next character from buffer
- `0` if buffer is empty

**Behavior:**
1. Saves interrupt state
2. Checks if buffer empty → returns 0
3. Reads character at tail position
4. Advances tail pointer (circular)
5. Restores interrupt state

**Example:**
```c
// Terminal transmit loop
while (pty_has_out(pty)) {
    char c = pty_read_out(pty);
    uart_putc(c);
}
```

#### `int pty_has_out(struct pty *p)`
Check if output buffer has data available.

**Returns:**
- Non-zero if data available
- Zero if buffer empty

**Thread Safety:** Interrupt-safe.

**Use Case:** Terminal TX ready polling.

### Line Discipline

#### `int pty_getline(struct pty *p, char *buf, int max_len)`
Blocking line read with echo and basic line editing (canonical mode).

**Parameters:**
- `p`: PTY to read from
- `buf`: Destination buffer
- `max_len`: Maximum characters to read (including null terminator)

**Returns:** Number of characters read (excluding null terminator).

**Behavior:**
1. Loops until newline or buffer full:
   - **Blocks** if no input available (polls with 20ms sleep via scheduler)
   - Reads character from input buffer
   - **Newline (`\r`, `\n`):** Echoes newline and returns
   - **Backspace (`\b`, 127):** 
     - If buffer not empty: Removes last character
     - Echoes backspace-space-backspace sequence (VT100 erase)
   - **Printable characters:** Echoes character and stores in buffer
2. Null-terminates buffer
3. Returns character count

**Echo Protocol:** All input is echoed to the output buffer for terminal display.

**Blocking Mechanism:** Uses `task_block_current_until()` to sleep task until input arrives.

**Thread Safety:** Should only be called from a single task per PTY.

**Example:**
```c
// Shell main loop with PTY
char line[256];
int len = pty_getline(pty, line, sizeof(line));
if (len > 0) {
    execute_command(line);
}
```

## Implementation Details

### Ring Buffer Algorithm

Both input and output buffers use the same ring buffer implementation:

**Empty Condition:** `head == tail`
```
[  |  |  |  |  ]
   ^
   head/tail
```

**Partial Fill:** `head > tail`
```
[  |XX|XX|XX|  ]
   ^        ^
   tail    head
```

**Wraparound:** `head < tail` (head wrapped around)
```
[XX|XX|  |  |XX]
      ^     ^
      head  tail
```

**Full Detection:** `(head + 1) % size == tail`
```
[XX|XX|XX|XX|XX]
   ^  ^
   t  h  (h+1)%size == t
```

**Write Operation:**
```c
int next = (head + 1) % size;
if (next != tail) {
    buffer[head] = c;
    head = next;
}
```

**Read Operation:**
```c
if (head != tail) {
    char c = buffer[tail];
    tail = (tail + 1) % size;
    return c;
}
return 0;
```

### Buffer Sizing Rationale

**Input Buffer (512 bytes):**
- Typical line length: 80-256 characters
- Allows buffering of multiple short commands
- Prevents overflow during burst input

**Output Buffer (2048 bytes):**
- Shell commands can produce significant output
- Prevents backpressure on shell during slow terminal transmission
- 4x input size handles typical command output

### Canonical Mode Implementation

`pty_getline()` implements a simplified canonical mode (cooked mode):

**Features:**
- Line buffering (return on newline)
- Backspace/delete handling with visual feedback
- Character echo for user feedback

**Missing Features (vs. Full TTY):**
- No special character handling (^C, ^Z, ^D)
- No line kill (^U) or word erase (^W)
- No signal generation
- No flow control (^S/^Q)

**Design Choice:** Simplicity over full POSIX compatibility. Sufficient for interactive shell use.

### Interrupt Safety

All buffer operations use interrupt-safe access:

```c
unsigned long flags = irq_save();  // Save interrupt state, disable IRQs
// ... critical section ...
irq_restore(flags);                 // Restore interrupt state
```

**Why Needed:**
- PTY may be accessed from IRQ handlers (e.g., UART RX interrupt)
- Prevents race conditions on buffer pointers
- Ensures atomic buffer state updates

**Current Limitation:** The `lock` field is present but unused. Interrupt disabling provides sufficient protection for single-core operation.

### Blocking and Scheduling Integration

`pty_getline()` integrates with the MYRASPOS scheduler:

```c
while (!pty_has_in(p)) {
    task_block_current_until(scheduler_get_tick() + 20);
}
```

**Behavior:**
1. Task checks for input
2. If no input: Blocks task until `current_tick + 20ms`
3. Scheduler runs other tasks during wait
4. Task wakes after 20ms, checks again

**Efficiency:** Prevents busy-waiting while maintaining responsiveness (20ms latency).

## Design Decisions

### Why Ring Buffers?
**Decision:** Use circular ring buffers for both input and output.

**Rationale:**
- Fixed memory footprint (no dynamic allocation during I/O)
- O(1) read and write operations
- Simple implementation without complex data structures
- Cache-friendly for small buffers

**Trade-off:** Fixed size means overflow drops data (acceptable for terminal I/O).

### Why Asymmetric Buffer Sizes?
**Decision:** Input=512 bytes, Output=2048 bytes.

**Rationale:**
- User input is typically short (commands, lines)
- Shell output can be verbose (ls, cat, logs)
- Prevents shell blocking on slow terminal transmission
- 4:1 ratio handles typical command output patterns

**Evidence:** Most commands produce more output than their command line length.

### Why Blocking getline()?
**Decision:** Implement blocking canonical read rather than non-blocking character-by-character.

**Rationale:**
- Simplifies shell implementation (no manual line buffering)
- Provides standard readline-like interface
- Matches Unix terminal semantics
- Reduces shell code complexity

**Alternative:** Non-blocking read would require shell to implement line buffering.

### Why Interrupt Disable vs. Spinlock?
**Decision:** Use `irq_save()`/`irq_restore()` instead of spinlock.

**Rationale:**
- Single-core system: Interrupt disable is sufficient
- Simpler than spinlock implementation
- Lower overhead (no atomic operations needed)
- Prevents deadlock scenarios

**Future:** The `lock` field is reserved for future SMP support.

### Why Drop on Overflow?
**Decision:** Silently drop characters when buffer full.

**Rationale:**
- Simpler than complex backpressure mechanisms
- Prevents system deadlock
- Rare in practice (buffers sized appropriately)
- Alternative (blocking) would require flow control

**Trade-off:** Data loss possible, but system remains stable.

### Why 20ms Poll Interval?
**Decision:** Poll input buffer every 20ms in `pty_getline()`.

**Rationale:**
- Balances responsiveness vs. CPU overhead
- Typical typing speed: 5-10 char/sec → 100-200ms between chars
- 20ms latency imperceptible to user
- Allows scheduler to run other tasks efficiently

**Calculation:** 20ms @ 50Hz tick rate = 1 tick sleep

## Usage Examples

### Basic PTY Session Setup
```c
// Create PTY for new terminal session
struct pty *pty = pty_alloc();
if (!pty) {
    uart_puts("PTY allocation failed\n");
    return;
}

// Start shell on PTY
int pid = task_create(shell_main, pty, "shell-pty");
```

### Terminal Client Input Loop
```c
// UART RX interrupt handler
void uart_rx_handler(void) {
    if (uart_has_data()) {
        char c = uart_getc();
        if (active_pty) {
            pty_write_in(active_pty, c);
        }
    }
}
```

### Terminal Client Output Loop
```c
// Main loop transmit
while (1) {
    if (pty_has_out(pty)) {
        char c = pty_read_out(pty);
        uart_putc(c);
    }
    yield();
}
```

### Shell Integration
```c
// Shell using PTY for I/O
void shell_main(void *arg) {
    struct pty *pty = (struct pty *)arg;
    char line[256];
    
    // Write prompt
    const char *prompt = "$ ";
    for (const char *s = prompt; *s; ++s) {
        pty_write_out(pty, *s);
    }
    
    // Read command
    int len = pty_getline(pty, line, sizeof(line));
    
    // Execute and write output
    char output[1024];
    int out_len = execute_command(line, output, sizeof(output));
    for (int i = 0; i < out_len; ++i) {
        pty_write_out(pty, output[i]);
    }
}
```

### Multi-Session Server
```c
#define MAX_SESSIONS 4
struct session {
    struct pty *pty;
    int shell_pid;
    int active;
};

struct session sessions[MAX_SESSIONS];

int create_session(void) {
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (!sessions[i].active) {
            sessions[i].pty = pty_alloc();
            if (!sessions[i].pty) return -1;
            
            sessions[i].shell_pid = task_create(shell_main, 
                                               sessions[i].pty, 
                                               "shell");
            sessions[i].active = 1;
            return i;
        }
    }
    return -1;  // No slots available
}

void destroy_session(int id) {
    if (id >= 0 && id < MAX_SESSIONS && sessions[id].active) {
        task_kill(sessions[id].shell_pid);
        pty_free(sessions[id].pty);
        sessions[id].active = 0;
    }
}
```

### Echo Test Program
```c
// Simple PTY echo test
void pty_echo_test(struct pty *p) {
    const char *msg = "Type something (echo mode):\n";
    for (const char *s = msg; *s; ++s) {
        pty_write_out(p, *s);
    }
    
    for (int i = 0; i < 10; ++i) {
        while (!pty_has_in(p)) {
            yield();
        }
        char c = pty_read_in(p);
        pty_write_out(p, c);  // Echo back
        
        if (c == '\n') break;
    }
}
```

## Cross-References

### Related Services
- **[shell.md](shell.md)** - Shell service (primary PTY consumer)
- **[service.md](service.md)** - Service manager (can spawn PTY sessions)
- **[syscall.md](syscall.md)** - System call interface integration

### Related Kernel Components
- **sched.h/c** - Task scheduler for `task_block_current_until()`
- **irq.h/c** - Interrupt management for `irq_save()`/`irq_restore()`
- **kmalloc.h/c** - Memory allocator for PTY structure
- **uart.h/c** - Serial I/O (typical PTY backend)
- **init.h/c** - Initialization (task_get_tty integration)

### API Integration Points

**Shell Integration:**
```c
// shell.c: Detect PTY and use for I/O
struct pty *pty = (struct pty *)arg;
if (pty) {
    pty_write_out(pty, 'X');  // Write to PTY
    char c = pty_read_in(pty); // Read from PTY
    int len = pty_getline(pty, buf, size);  // Readline
} else {
    init_puts("X");            // Fall back to console
    char c = init_getc();
}
```

**Syscall Integration:**
```c
// syscall.c: Redirect output based on task's TTY
struct pty *p = task_get_tty(task_current_id());
if (p) {
    // Write to PTY
    for (const char *s = str; *s; ++s) {
        pty_write_out(p, *s);
    }
} else {
    // Write to console
    uart_puts(str);
}
```

### Buffer Constants
```c
#define PTY_IN_SIZE  512    // Input buffer size
#define PTY_OUT_SIZE 2048   // Output buffer size
#define PTY_POLL_MS  20     // Getline polling interval
```

## Thread Safety

**Interrupt-Safe Operations:**
- `pty_write_in()`
- `pty_read_in()`
- `pty_write_out()`
- `pty_read_out()`
- `pty_has_in()`
- `pty_has_out()`

**Not Thread-Safe:**
- `pty_getline()` - Should only be called from one task per PTY

**Concurrency Model:**
- Multiple PTYs can be used concurrently (independent data structures)
- Single PTY shared between exactly two contexts:
  - One writer to input (terminal client/IRQ handler)
  - One reader from input (shell task)
  - One writer to output (shell task)
  - One reader from output (terminal client)

## Performance Considerations

### Memory Footprint
- Per-PTY overhead: 2560 bytes (512 + 2048)
- Plus structure overhead: ~20 bytes
- **Total per PTY:** ~2580 bytes

### CPU Overhead
- Write/read operations: O(1) - single array access
- Buffer full/empty check: O(1) - integer comparison
- `pty_getline()`: Blocks task, no CPU usage while waiting

### Latency
- Character echo latency: <1ms (immediate)
- Read latency: Up to 20ms (polling interval)
- Write latency: <1ms (buffer operation)

### Optimization Opportunities
- **Adaptive polling:** Reduce interval when input active
- **Event-driven model:** Use semaphores instead of polling
- **Buffer expansion:** Dynamic buffers for high-throughput sessions
- **Lock-free algorithms:** For future SMP support

### Typical Performance
- **Terminal typing:** ~10 char/sec → negligible overhead
- **Command output:** ~10KB/sec → 5 buffer flushes/sec
- **Max throughput:** Limited by application, not PTY (buffers sized appropriately)

## Error Handling

### Buffer Overflow
**Behavior:** Silently drops characters when buffer full.

**Detection:** None (silent drop by design).

**Mitigation:**
- Size buffers appropriately for use case
- Applications should limit output bursts
- Terminal clients should drain output buffer regularly

### Allocation Failure
**Behavior:** `pty_alloc()` returns NULL.

**Handling:** Caller must check return value and handle gracefully.

**Example:**
```c
struct pty *p = pty_alloc();
if (!p) {
    log_error("PTY allocation failed");
    return -ENOMEM;
}
```

### Invalid Parameters
**Behavior:** NULL pointer checks return immediately (no-op or 0).

**Safe Functions:**
- `pty_free(NULL)` - No-op
- `pty_write_in(NULL, c)` - No-op
- `pty_read_in(NULL)` - Returns 0

### Race Conditions
**Prevention:** All buffer operations use interrupt-safe critical sections.

**Guarantee:** Buffer state always consistent, even under concurrent access from IRQ handlers.
