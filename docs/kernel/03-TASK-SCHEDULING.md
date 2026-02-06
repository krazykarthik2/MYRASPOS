# Task Scheduling and Context Switching Documentation

## Overview

The MYRASPOS scheduler implements **cooperative preemptive multitasking** with a **round-robin** scheduling policy. Tasks are organized in a circular linked list and scheduled fairly, with support for blocking operations, event waiting, and timed sleeps.

**Files**:
- `kernel/sched.c` (878 lines) - Scheduler implementation
- `kernel/sched.h` (65 lines) - Scheduler API and structures
- `kernel/swtch.S` (57 lines) - Context switching assembly

## Core Data Structures

### Task Structure
```c
struct task {
    // Identification
    int id;                      // Unique task ID (0 = boot, 1+ = user tasks)
    char name[16];               // Human-readable task name
    uint32_t magic;              // 0xDEADC0DE (corruption detection)
    
    // Execution state
    task_fn fn;                  // Entry function (NULL = blocked)
    void *arg;                   // Argument passed to fn
    int run_count;               // Number of times scheduled
    int start_tick;              // Creation timestamp (ms)
    int is_running;              // Currently executing flag
    int zombie;                  // Marked for cleanup
    
    // Blocking
    task_fn saved_fn;            // Saved fn when blocked
    uint32_t wake_tick;          // When to wake (ms since boot)
    enum block_reason block_type; // BLOCK_NONE/BLOCK_TIMER/BLOCK_EVENT
    
    // Memory
    void *stack;                 // Kernel stack allocation
    size_t stack_total_bytes;    // Total allocation (guard + stack)
    struct task_context context; // Saved CPU registers
    uint64_t *pgd;              // Page table (NULL = kernel task)
    
    // Relationships
    int parent_id;               // Parent task ID (for cleanup)
    struct task *next;           // Circular linked list
    
    // I/O
    void *tty;                   // Associated pseudo-terminal
};
```

**Design Decisions**:

1. **Circular Linked List**: Simple O(1) insertion and rotation, but O(N) lookup
   - **Trade-off**: Simplicity over speed for small task counts

2. **Function Pointer as Blocking Indicator**: `fn == NULL` means blocked
   - **Trade-off**: Simple check, but requires saving/restoring function pointer

3. **Magic Number**: `0xDEADC0DE` detects memory corruption
   - **Constraint**: Must check on every context switch

4. **Parent Tracking**: Enables recursive task cleanup
   - **Design**: When parent dies, kill all children

### Task Context (CPU State)
```c
struct task_context {
    uint64_t x19;  // Callee-saved registers
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;  // Frame pointer
    uint64_t x30;  // Link register (return address)
    uint64_t sp;   // Stack pointer
};
```

**Why Only x19-x30 and SP?**

**ARM64 Calling Convention**:
- x0-x18: Caller-saved (function arguments, scratch registers)
- x19-x28: Callee-saved (must be preserved across calls)
- x29: Frame pointer
- x30: Link register (return address)
- SP: Stack pointer

**Design Decision**: Only save callee-saved registers because:
1. Caller-saved registers (x0-x18) are already saved by calling function
2. Context switch happens via function call, so calling convention applies
3. Saves memory and time (13 registers vs 31)

**Constraint**: Tasks must not corrupt x0-x18 across system calls

## Task Creation

### Task Creation API
```c
int task_create(task_fn fn, void *arg, const char *name);
int task_create_with_stack(task_fn fn, void *arg, 
                           const char *name, size_t stack_kb);
int task_create_user(const char *name, void *entry_point);
```

### Stack Layout
```
High Address
┌─────────────────────────┐
│ Stack Guard Page (4KB)  │ ← Detects underflow
├─────────────────────────┤
│ Stack Canary Value      │ ← 0xDEADBEEFCAFEBABE
│  (detects overflow)     │
├─────────────────────────┤
│                         │
│   Usable Stack Space    │ ← SP grows downward
│   (16KB or 64KB)        │
│                         │
└─────────────────────────┘ ← Stack top (16-byte aligned)
Low Address
```

**Stack Sizes**:
- **Regular tasks**: 16KB (default)
- **Init task**: 64KB (needs more for VirtIO + service bootup)

**Design Decision**: Larger init stack because:
- Initializes VirtIO drivers (large buffers)
- Starts services (deep call stacks)
- Loads assets from disk

**Constraints**:
1. **16-byte alignment**: ARM64 ABI requires SP aligned to 16 bytes
2. **Guard page**: 4KB guard page at bottom detects underflow
3. **Canary**: Magic value at top detects overflow
4. **No expansion**: Stacks are fixed size, no dynamic growth

### Task Initialization Process

**Step 1: Allocate Task Structure**
```c
struct task *t = kmalloc(sizeof(*t));
memset(t, 0, sizeof(*t));
```

**Step 2: Allocate Stack with Guard**
```c
#define STACK_GUARD_SIZE 4096
size_t stack_size = stack_kb * 1024;
size_t total_alloc = STACK_GUARD_SIZE + stack_size;
t->stack = kmalloc(total_alloc);
```

**Step 3: Initialize Stack**
```c
char *stack_bottom = (char*)t->stack + STACK_GUARD_SIZE;
uint64_t *canary = (uint64_t*)stack_bottom;
*canary = 0xDEADBEEFCAFEBABE;  // Stack canary

char *stack_end = stack_bottom + stack_size;
uint64_t aligned_sp = ((uint64_t)stack_end) & ~0xF;  // 16-byte align
```

**Step 4: Setup Initial Context**
```c
t->context.x19 = (uint64_t)fn;           // Function pointer
t->context.x20 = (uint64_t)arg;          // Argument
t->context.x30 = (uint64_t)ret_from_fork; // Return address
t->context.sp = aligned_sp;               // Stack pointer
```

**Design Decision**: Initial context points to `ret_from_fork()` which:
1. Enables interrupts (`msr daifclr, #2`)
2. Loads argument into x0 (`mov x0, x20`)
3. Calls function (`blr x19`)
4. Calls `task_exit(0)` when function returns

**Step 5: Insert into Task List**
```c
t->id = next_task_id++;
strcpy(t->name, name);
t->magic = 0xDEADC0DE;
t->parent_id = task_current_id();

if (!task_head) {
    task_head = t;
    t->next = t;  // Circular: points to itself
} else {
    t->next = task_head->next;
    task_head->next = t;
}
```

**Design Decision**: Circular list simplifies rotation:
- No NULL checks needed
- Always have a "next" task
- Head points to most recently inserted

## Scheduling Algorithm

### Main Scheduling Loop
```c
void schedule(void) {
    1. reap_zombies();           // Cleanup exited tasks
    2. timer_poll_and_advance(); // Update system time
    3. irq_poll_and_dispatch();  // Check for I/O
    4. Select next runnable task
    5. Context switch if different task
    6. Return (now running new task)
}
```

**Called From**:
- `kernel_main()`: Main loop calls repeatedly
- `yield()`: Explicit task yield
- Implicitly after `task_exit()` via re-entry to schedule loop

### Task Selection Logic
```c
static struct task* find_next_runnable(struct task *start) {
    struct task *t = start->next;
    int iterations = 0;
    
    while (1) {
        // Check if runnable: fn != NULL and not zombie
        if (t->fn && !t->zombie) {
            return t;
        }
        
        t = t->next;
        iterations++;
        
        // Prevent infinite loop if no tasks runnable
        if (iterations > 1000 || t == start) {
            return &boot_task;  // Fallback to boot task
        }
    }
}
```

**Round-Robin Policy**:
- Start from current task's next
- Take first runnable task found
- Circular list ensures fairness over time

**Design Constraint**: If NO tasks runnable, return to boot task (idle loop)

### Context Switch

**High-Level Flow**:
```c
if (next != task_cur) {
    // Validate stack integrity
    check_stack_guards(task_cur);
    check_stack_guards(next);
    
    // Mark states
    task_cur->is_running = 0;
    next->is_running = 1;
    
    // Disable interrupts (atomic switch)
    unsigned long flags = irq_save();
    
    // Switch contexts (assembly)
    cpu_switch_to(&task_cur->context, &next->context);
    
    // Re-enable interrupts
    irq_restore(flags);
    
    // Update current pointer
    task_cur = next;
}
```

### Context Switch Assembly (`cpu_switch_to` in swtch.S)

```asm
// x0 = &prev->context, x1 = &next->context

// Save previous task's context
stp x19, x20, [x0, #0]     // Store pair: x19, x20 at offset 0
stp x21, x22, [x0, #16]    // Store pair: x21, x22 at offset 16
stp x23, x24, [x0, #32]
stp x25, x26, [x0, #48]
stp x27, x28, [x0, #64]
stp x29, x30, [x0, #80]
mov x9, sp
str x9, [x0, #96]          // Store SP at offset 96

// Load next task's context
ldp x19, x20, [x1, #0]     // Load pair: x19, x20 from offset 0
ldp x21, x22, [x1, #16]
ldp x23, x24, [x1, #32]
ldp x25, x26, [x1, #48]
ldp x27, x28, [x1, #64]
ldp x29, x30, [x1, #80]
ldr x9, [x1, #96]          // Load SP from offset 96
mov sp, x9

ret                        // Return to x30 (link register)
```

**Design Details**:
1. **stp/ldp**: Store/load pair (8 bytes each) - efficient
2. **Offsets**: Each register pair at 16-byte intervals
3. **SP handling**: Must use intermediate register (x9)
4. **Return**: `ret` jumps to x30 (saved return address)

**First Run**: When task first scheduled:
- x30 = `ret_from_fork` (set during creation)
- `ret` jumps to `ret_from_fork()`
- Executes: enable IRQs, call function

**Subsequent Runs**: When task yields/preempted:
- x30 = address after last `cpu_switch_to` call
- `ret` returns to that point
- Continues execution

### ret_from_fork (First Task Entry)
```asm
ret_from_fork:
    msr daifclr, #2      // Enable IRQs (clear IRQ mask bit)
    mov x0, x20          // x0 = arg (from x20)
    blr x19              // Call function (x19 = fn)
    // Function returned, exit task
    bl task_exit         // Never returns
```

**Design Decision**: Enable IRQs only AFTER context switch completes
- **Reason**: Prevent re-entrant context switches
- **Constraint**: IRQs disabled during `cpu_switch_to()`

## Blocking and Waiting

### Timer-Based Blocking (Sleep)
```c
void task_block_current_until(uint32_t wake_tick) {
    task_cur->wake_tick = wake_tick;
    task_cur->block_type = BLOCK_TIMER;
    task_cur->saved_fn = task_cur->fn;
    task_cur->fn = NULL;  // Mark as blocked
}
```

**Usage**:
```c
// Sleep for 100ms
uint32_t wake_time = scheduler_get_tick() + 100;
task_block_current_until(wake_time);
yield();  // Give up CPU
```

**Wakeup**:
```c
void scheduler_tick_advance(uint32_t delta_ms) {
    scheduler_tick += delta_ms;
    
    // Wake sleeping tasks
    for each task t:
        if (t->block_type == BLOCK_TIMER &&
            t->wake_tick <= scheduler_tick) {
            t->fn = t->saved_fn;  // Unblock
            t->block_type = BLOCK_NONE;
        }
}
```

**Design Decision**: Use monotonic tick counter (ms since boot)
- **Advantage**: No wraparound issues for reasonable uptimes
- **Constraint**: 32-bit counter wraps after ~49 days

### Event-Based Blocking (Wait/Wake)
```c
struct event_waiter {
    int task_id;
    void *event_id;  // Arbitrary identifier
    struct event_waiter *next;
};
static struct event_waiter *wait_list = NULL;
```

**Wait on Event**:
```c
void task_wait_event(void *event_id) {
    struct event_waiter *w = kmalloc(sizeof(*w));
    w->task_id = task_cur->id;
    w->event_id = event_id;
    w->next = wait_list;
    wait_list = w;
    
    task_cur->block_type = BLOCK_EVENT;
    task_cur->saved_fn = task_cur->fn;
    task_cur->fn = NULL;  // Block
}
```

**Wake Event**:
```c
void task_wake_event(void *event_id) {
    struct event_waiter *w = wait_list;
    struct event_waiter *prev = NULL;
    
    while (w) {
        if (w->event_id == event_id) {
            // Unblock task
            struct task *t = find_task_by_id(w->task_id);
            if (t && t->fn == NULL) {
                t->fn = t->saved_fn;
                t->block_type = BLOCK_NONE;
            }
            
            // Remove from wait list
            if (prev) prev->next = w->next;
            else wait_list = w->next;
            
            struct event_waiter *to_free = w;
            w = w->next;
            kfree(to_free);
        } else {
            prev = w;
            w = w->next;
        }
    }
}
```

**Standard Event IDs**:
```c
#define WM_EVENT_ID    ((void*)0x100)   // Window manager events
#define MOUSE_EVENT_ID ((void*)0x200)   // Mouse events
```

**Design Decision**: Use small integer casts to pointers as event IDs
- **Reason**: Avoids string literal address mismatches
- **Constraint**: Event IDs must be unique across codebase

## Task Lifecycle

### Task States
```
                    ┌─────────────┐
                    │   Created   │
                    └──────┬──────┘
                           │ task_create()
                           ↓
                    ┌─────────────┐
        ┌──────────>│  Runnable   │<─────────┐
        │           └──────┬──────┘          │
        │                  │ schedule()       │
        │                  ↓                  │
        │           ┌─────────────┐          │
        │ yield()   │   Running   │          │ wake
        └───────────┤             ├──────────┘
                    └──────┬──────┘
                           │ block/wait
                           ↓
                    ┌─────────────┐
                    │   Blocked   │
                    └──────┬──────┘
                           │ task_kill() / task_exit()
                           ↓
                    ┌─────────────┐
                    │   Zombie    │
                    └──────┬──────┘
                           │ reap_zombies()
                           ↓
                    ┌─────────────┐
                    │   Freed     │
                    └─────────────┘
```

### Task Exit and Cleanup
```c
void task_exit(int code) {
    task_cur->zombie = 1;        // Mark for cleanup
    task_cur->fn = NULL;         // Make non-runnable
    kill_children_of(task_cur->id);  // Recursive cleanup
    
    yield();  // Give up CPU (never returns here)
}
```

**Design Constraint**: Cannot free task while on its own stack!
- **Solution**: Mark as zombie, free later during `reap_zombies()`

### Zombie Reaping
```c
static void reap_zombies(void) {
    struct task *t = task_head;
    struct task *prev = NULL;
    
    // Iterate circular list
    do {
        struct task *next = t->next;
        
        if (t->zombie && !t->is_running) {
            // Safe to free (not on this stack)
            if (prev) prev->next = next;
            else task_head = next;
            
            kfree(t->stack);
            kfree(t);
            
            if (task_head == t) task_head = NULL;
            return;  // Only reap one per cycle
        }
        
        prev = t;
        t = next;
    } while (t != task_head);
}
```

**Design Decision**: Reap only one zombie per schedule cycle
- **Reason**: Limits time spent in cleanup
- **Trade-off**: Zombies may linger for multiple cycles

## Stack Protection

### Guard Page (Underflow Detection)
```c
#define STACK_GUARD_SIZE 4096
t->stack = kmalloc(STACK_GUARD_SIZE + stack_size);
```

**Purpose**: 4KB gap at bottom of stack
**Detection**: Page fault if accessed (not currently implemented in MMU)
**Future**: Map guard page as no-access in MMU

### Stack Canary (Overflow Detection)
```c
uint64_t *canary = (uint64_t*)(t->stack + STACK_GUARD_SIZE);
*canary = 0xDEADBEEFCAFEBABE;
```

**Check on Context Switch**:
```c
static void check_stack_guards(struct task *t) {
    uint64_t *canary = (uint64_t*)((char*)t->stack + STACK_GUARD_SIZE);
    if (*canary != 0xDEADBEEFCAFEBABE) {
        panic("Stack overflow detected in task %s", t->name);
    }
}
```

**Design Decision**: Check every context switch
- **Trade-off**: Performance cost vs safety
- **Constraint**: Must check before freeing task

## Preemption

### Cooperative Preemption Model
```c
static volatile int preempt_requested = 0;

void scheduler_request_preempt(void) {
    preempt_requested = 1;
}
```

**IRQ Handler**:
```c
void irq_handler() {
    handle_interrupt();
    scheduler_request_preempt();  // Request preemption
}
```

**Scheduling Loop**:
```c
void schedule(void) {
    if (preempt_requested) {
        preempt_requested = 0;
        // Normal scheduling continues
    }
    // ...select next task...
}
```

**Design Decision**: Request preemption, don't force it
- **Reason**: Avoids complex interrupt-context switching
- **Trade-off**: Higher latency, but simpler and safer

**Constraint**: Tasks must call `schedule()` or `yield()` regularly
- **Otherwise**: System appears frozen
- **Mitigation**: Timer interrupts request preemption, eventually honored

## Performance Characteristics

### Time Complexity
| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Task create | O(1) | Append to list |
| Task kill | O(N) | Find in list |
| Find next runnable | O(N) | Linear scan |
| Context switch | O(1) | Register save/restore |
| Reap zombie | O(N) | List traversal |
| Wake event | O(W) | W = waiters on event |

**Design Trade-off**: Simplicity over speed
- **Justification**: Embedded system with <100 tasks typically

### Memory Usage
| Item | Size | Notes |
|------|------|-------|
| Task struct | ~272 bytes | Per task |
| Default stack | 16 KB | Per regular task |
| Init stack | 64 KB | Init task only |
| Event waiter | 24 bytes | Per waiting task |

**Total per task**: ~16.3 KB (default)

## Design Constraints Summary

### Hard Constraints
1. **Stack alignment**: Must be 16-byte aligned (ARM64 ABI)
2. **No FP registers**: Can't use NEON/FPU (not saved)
3. **IRQs disabled during switch**: Atomic context switch
4. **Can't free running task**: Reap after switch
5. **Circular list**: Must handle wrap-around

### Soft Constraints
1. **Maximum tasks**: Limited by memory (~1000s)
2. **Stack size**: Fixed at creation, no growth
3. **Scheduling fairness**: Best-effort round-robin
4. **Preemption latency**: Depends on task yield frequency

## Debug and Instrumentation

### Task Statistics
```c
int task_stats(int *ids_out, int *run_counts_out, 
               int *start_ticks_out, int *runnable_out,
               char *names_out, int max, int *total_runs_out);
```

**Usage**: `ps` command shows task list

### Scheduler Debugging
```c
void scheduler_switch_debug(uint64_t lr, uint64_t sp);
void scheduler_ret_from_fork_debug(void);
```

**Purpose**: Trace context switches and task starts

## See Also
- [Architecture Overview](../00-ARCHITECTURE.md)
- [Boot Sequence](../01-BOOT-SEQUENCE.md)
- [Memory Management](02-MEMORY-MANAGEMENT.md)
- [Interrupt Handling](../04-INTERRUPT-HANDLING.md)
- [System Calls](syscall.md)
