# Input Subsystem Documentation (input.c/h)

## Overview

The input subsystem provides keyboard and mouse input event management for MYRASPOS. It implements event queuing, mouse state tracking, and coordinate normalization for both absolute (tablet) and relative (mouse) input devices, interfacing primarily with VirtIO input devices.

**Hardware Interface**: VirtIO Input (keyboard, mouse, tablet)  
**Event Model**: Linux input event compatible  
**Coordinate System**: Screen-space absolute coordinates  
**Queue Model**: Separate queues for keyboard and mouse events

## Hardware Details

### Input Event Protocol

The subsystem uses Linux-compatible input event structure:

```c
struct input_event {
    uint16_t type;    // Event type (KEY, REL, ABS, etc.)
    uint16_t code;    // Event code (key code, axis, etc.)
    int32_t value;    // Event value (pressed/released, delta, position)
};
```

### Event Types

| Type | Value | Description |
|------|-------|-------------|
| INPUT_TYPE_SYN | 0 | Synchronization (frame boundary) |
| INPUT_TYPE_KEY | 1 | Keyboard keys and mouse buttons |
| INPUT_TYPE_REL | 2 | Relative axes (mouse movement) |
| INPUT_TYPE_ABS | 3 | Absolute axes (tablet/touchscreen) |
| INPUT_TYPE_MSC | 4 | Miscellaneous |
| INPUT_TYPE_MOUSE_BTN | 10 | Internal: normalized mouse button |

### Key/Button Codes

**Keyboard Keys**: 0x00-0xFF (standard Linux keycodes)  
**Mouse Buttons**: 0x110-0x117
- `0x110`: Left button (BTN_LEFT)
- `0x111`: Right button (BTN_RIGHT)
- `0x112`: Middle button (BTN_MIDDLE)

**Key Values**:
- `0`: Released
- `1`: Pressed
- `2`: Repeat (held)

### Coordinate Spaces

**VirtIO Absolute (Tablet)**:
- Range: 0-32767 (INPUT_TYPE_ABS)
- Code 0: X-axis
- Code 1: Y-axis

**Relative Movement (Mouse)**:
- Range: Signed delta (INPUT_TYPE_REL)
- Code 0: X-axis delta
- Code 1: Y-axis delta

**Screen Space**:
- Range: 0 to (width-1), 0 to (height-1)
- After normalization and clamping

## Key Data Structures

### Event Queues

```c
#define EVENT_QUEUE_SIZE 256

static struct input_event key_queue[EVENT_QUEUE_SIZE];
static int key_head = 0, key_tail = 0;

static struct input_event mouse_queue[EVENT_QUEUE_SIZE];
static int mouse_head = 0, mouse_tail = 0;
```

**Queue Type**: Ring buffer  
**Capacity**: 256 events per queue  
**Overflow Behavior**: Drop new events when full

### Mouse State

```c
static int mouse_x = 0, mouse_y = 0, mouse_btn = 0;
static int screen_w = 800, screen_h = 600;
```

**Purpose**: Low-latency cursor position for rendering  
**Update**: Immediately on event push (before queuing)  
**Thread Safety**: Protected by `irq_save()`/`irq_restore()`

### Lock Implementation

```c
static volatile int input_lock = 0;
```

**Type**: Spinlock using ARMv8 exclusive load/store  
**Scope**: Single-core optimized (busy-wait minimal)  
**Combined**: With IRQ disable for full protection

## Key Functions

### Initialization

#### `void input_init(int screen_w, int screen_h)`
**Purpose**: Initialize input subsystem with screen dimensions  
**Signature**: `void input_init(int screen_w, int screen_h)`  
**Parameters**:
- `screen_w` - Screen width in pixels
- `screen_h` - Screen height in pixels

**Behavior**:
- Store screen dimensions for coordinate normalization
- Initialize mouse position to center of screen
- Reset button state to 0 (released)

**Implementation**:
```c
void input_init(int sw, int sh) {
    screen_w = sw;
    screen_h = sh;
    mouse_x = sw / 2;
    mouse_y = sh / 2;
}
```

**When to Call**: After framebuffer initialization, before starting window manager

### Event Submission

#### `void input_push_event(uint16_t type, uint16_t code, int32_t value)`
**Purpose**: Submit input event to appropriate queue  
**Signature**: `void input_push_event(uint16_t type, uint16_t code, int32_t value)`  
**Parameters**:
- `type` - Event type (KEY, REL, ABS, MOUSE_BTN)
- `code` - Event code (key, axis, button)
- `value` - Event value (state, delta, position)

**Algorithm**:
1. **Update Global State**: Immediately update mouse position/button for low latency
2. **Route to Queue**: Determine keyboard vs mouse queue
3. **Enqueue Event**: Add to ring buffer if space available
4. **Wake Tasks**: Signal waiting tasks via event IDs

**Mouse State Update**:
```c
if (type == INPUT_TYPE_ABS) {
    if (code == 0) mouse_x = scale_mouse(value, screen_w);
    else if (code == 1) mouse_y = scale_mouse(value, screen_h);
} else if (type == INPUT_TYPE_REL) {
    if (code == 0) mouse_x += value;
    else if (code == 1) mouse_y += value;
} else if (type == INPUT_TYPE_KEY && code >= 0x110) {
    if (code == 0x110) mouse_btn = value;
}

// Clamp to screen bounds
if (mouse_x < 0) mouse_x = 0;
if (mouse_x >= screen_w) mouse_x = screen_w - 1;
if (mouse_y < 0) mouse_y = 0;
if (mouse_y >= screen_h) mouse_y = screen_h - 1;
```

**Queue Routing**:
- `INPUT_TYPE_KEY` with code < 0x100: Keyboard queue
- `INPUT_TYPE_KEY` with code ≥ 0x100: Mouse queue (as MOUSE_BTN)
- `INPUT_TYPE_REL`: Mouse queue
- `INPUT_TYPE_ABS`: Mouse queue
- Other types: Mouse queue (default)

**Task Wakeup**:
```c
if (type == INPUT_TYPE_KEY) {
    task_wake_event(WM_EVENT_ID);
} else {
    task_wake_event(MOUSE_EVENT_ID);
    task_wake_event(WM_EVENT_ID);
}
```

### Event Retrieval

#### `int input_pop_key_event(struct input_event *ev)`
**Purpose**: Retrieve keyboard event from queue  
**Signature**: `int input_pop_key_event(struct input_event *ev)`  
**Parameters**:
- `ev` - Pointer to event structure to fill

**Returns**:
- `1` if event retrieved
- `0` if queue empty

**Behavior**:
- Lock queue access
- Check if events available
- Copy event to caller's structure
- Advance tail pointer (ring buffer)
- Unlock

**Thread-Safe**: Yes (uses spinlock + IRQ disable)

#### `int input_pop_mouse_event(struct input_event *ev)`
**Purpose**: Retrieve mouse event from queue  
**Signature**: Same as `input_pop_key_event()`

**Implementation**: Identical to key event, but uses mouse queue

#### `int input_pop_event(struct input_event *ev)`
**Purpose**: Legacy function - retrieve any event  
**Behavior**:
- Try keyboard queue first
- If empty, try mouse queue
- Return 0 if both empty

**Use Case**: Simplified event loop (doesn't distinguish input type)

### State Query

#### `void input_get_mouse_state(int *x, int *y, int *btn)`
**Purpose**: Get current mouse position and button state  
**Signature**: `void input_get_mouse_state(int *x, int *y, int *btn)`  
**Parameters**:
- `x` - Pointer to store X coordinate (may be NULL)
- `y` - Pointer to store Y coordinate (may be NULL)
- `btn` - Pointer to store button state (may be NULL)

**Returns**: Current state via pointer parameters

**Thread-Safe**: Yes (IRQ-protected read)

**Implementation**:
```c
void input_get_mouse_state(int *x, int *y, int *btn) {
    unsigned long flags = irq_save();
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (btn) *btn = mouse_btn;
    irq_restore(flags);
}
```

**Use Case**: Cursor rendering, hit testing

## Implementation Details

### Coordinate Normalization

#### Absolute Input (Tablet)

VirtIO tablet reports 0-32767 range. Must scale to screen:

```c
static int scale_mouse(int val, int max_res) {
    return (val * max_res) / 32768;
}
```

**Example** (800-pixel width):
```
Input: 16383 (center)
Output: (16383 * 800) / 32768 = 399 pixels
```

**Edge Cases**:
- Input 0 → Output 0
- Input 32767 → Output ~799 (one pixel from right edge)

#### Relative Input (Mouse)

Relative events are deltas added to current position:

```c
if (code == 0) mouse_x += value;  // X delta
else if (code == 1) mouse_y += value;  // Y delta
```

**Units**: Device-dependent (typically pixels or scaled units)

#### Clamping

After any update, coordinates clamped to screen:

```c
if (mouse_x < 0) mouse_x = 0;
if (mouse_x >= screen_w) mouse_x = screen_w - 1;
if (mouse_y < 0) mouse_y = 0;
if (mouse_y >= screen_h) mouse_y = screen_h - 1;
```

**Bounds**: [0, width-1] × [0, height-1]

### Spinlock Implementation

The input subsystem uses ARM exclusive load/store for spinlock:

```c
static unsigned long lock(void) {
    unsigned long flags = irq_save();
    unsigned int tmp;
    __asm__ volatile(
        "1: ldxr %w0, [%1]\n"        // Load exclusive
        "   cbnz %w0, 1b\n"           // Busy-wait if locked
        "   stxr %w0, %w2, [%1]\n"    // Store exclusive (1)
        "   cbnz %w0, 1b\n"           // Retry if failed
        "   dmb sy\n"                 // Data memory barrier
        : "=&r" (tmp)
        : "r" (&input_lock), "r" (1)
        : "memory"
    );
    return flags;
}
```

**Instructions**:
- `ldxr`: Load exclusive (mark cache line)
- `cbnz`: Compare and branch if non-zero
- `stxr`: Store exclusive (succeeds only if no other store occurred)
- `dmb sy`: Full memory barrier (ensures ordering)

**Purpose**: Atomic lock acquisition on single-core or SMP

**Combined Protection**: IRQ disable + spinlock prevents races with interrupt handlers

### Queue Management

Ring buffer with head/tail pointers:

```c
int next = (*head + 1) % EVENT_QUEUE_SIZE;
if (next != *tail) {
    q[*head] = event;
    *head = next;
}
```

**Full Condition**: `(head + 1) % size == tail`  
**Empty Condition**: `head == tail`  
**Capacity**: size - 1 events (one slot reserved to distinguish full/empty)

### Event Routing Logic

**Decision Tree**:
```
INPUT_TYPE_KEY
    ├─ code < 0x100 → Keyboard queue
    └─ code ≥ 0x100 → Mouse queue (convert to MOUSE_BTN)
INPUT_TYPE_REL → Mouse queue
INPUT_TYPE_ABS → Mouse queue
Other → Mouse queue
```

**Why Separate Queues?**:
- Keyboard and mouse handled by different consumers
- Window manager wants keyboard only
- Cursor renderer wants mouse only
- Prevents mouse events flooding keyboard queue

### Task Wakeup System

Events wake blocked tasks via IDs:

```c
#define WM_EVENT_ID      /* Window manager */
#define MOUSE_EVENT_ID   /* Cursor renderer */
```

**Mechanism**: Scheduler tracks blocked tasks waiting on event IDs. When event pushed, `task_wake_event(id)` unblocks all waiting tasks.

**Use Case**:
```c
// Task blocks waiting for input
task_wait_event(WM_EVENT_ID);

// VirtIO driver receives input
input_push_event(type, code, value);
// → Wakes waiting tasks
```

## Design Decisions

### Why Immediate State Update?

Mouse state updated **before** queuing event:

**Reason**: Cursor rendering needs lowest possible latency. Reading from global state (1-2 cycles) faster than dequeuing events (50+ cycles).

**Trade-off**: Event queue may not exactly reflect current state (state updates faster than queue consumption).

### Why Separate Key/Mouse Queues?

**Advantages**:
- Consumers can poll specific queue (efficiency)
- Prevents cross-contamination
- Different overflow policies possible
- Clearer code separation

**Alternative**: Single queue with type field (legacy `input_pop_event()` provides this)

### Why 256-Event Queues?

**Calculation**: At 1000 events/sec (high rate) with 100 Hz processing:
- Events per frame: 10
- Buffer: 256 provides 25× safety margin

**Overflow**: Rare except under extreme load (keyboard autorepeat flood)

### Why Normalize to Screen Space?

**Device Independence**: Applications work with screen coordinates, not device units

**Alternatives**:
- Application normalizes: Duplicates code, error-prone
- Device units: Requires knowledge of device range

**Chosen**: Kernel normalizes once, applications use directly

### Why Spinlock + IRQ Disable?

**Scenario**: VirtIO interrupt may call `input_push_event()` while application calls `input_pop_event()`

**Protection Needed**:
- Prevent interrupt during queue access: IRQ disable
- Prevent concurrent access (future SMP): Spinlock

**Combined**: Both mechanisms ensure atomicity

### Why Clamp Coordinates?

**Hardware Issue**: Relative mouse can accumulate deltas exceeding screen bounds

**Options**:
1. Wrap around (x > width → x = 0)
2. Clamp to bounds
3. Allow out-of-bounds

**Chosen**: Clamp (option 2) - cursor stays visible, predictable behavior

## Constraints

### Hardware Constraints

- **VirtIO-Specific**: Assumes VirtIO input event format
- **Single Device**: No support for multiple mice/keyboards
- **No Hotplug**: Devices must exist at boot
- **Limited Buttons**: Only left button tracked (mouse_btn)

### Software Constraints

- **Fixed Queue Size**: 256 events (not configurable)
- **No Priorities**: FIFO order, no event prioritization
- **No Filtering**: All events queued (no dead zone, no debouncing)
- **Single Button**: Only tracks one mouse button globally

### Design Constraints

- **Single-Core Optimized**: Spinlock efficient on single core, may contend on SMP
- **No Event Merging**: Consecutive moves not coalesced
- **No Gestures**: Raw events only, no high-level gesture recognition

## Usage Examples

### Initialize Input System

```c
#include "input.h"
#include "framebuffer.h"

void init_input(void) {
    int w, h;
    fb_get_res(&w, &h);
    input_init(w, h);
}
```

### Process Keyboard Events

```c
void handle_keyboard(void) {
    struct input_event ev;
    while (input_pop_key_event(&ev)) {
        if (ev.type == INPUT_TYPE_KEY) {
            if (ev.value == 1) {  // Key press
                uart_puts("Key pressed: ");
                uart_putu(ev.code);
                uart_puts("\n");
            }
        }
    }
}
```

### Process Mouse Events

```c
void handle_mouse(void) {
    struct input_event ev;
    while (input_pop_mouse_event(&ev)) {
        if (ev.type == INPUT_TYPE_MOUSE_BTN) {
            uart_puts("Button: ");
            uart_putu(ev.value);
            uart_puts("\n");
        }
    }
}
```

### Get Cursor Position

```c
void render_cursor(void) {
    int x, y, btn;
    input_get_mouse_state(&x, &y, &btn);
    
    draw_cursor_sprite(x, y);
    
    if (btn) {
        highlight_cursor(x, y);  // Show click feedback
    }
}
```

### Event Loop (Window Manager)

```c
void wm_event_loop(void) {
    while (1) {
        task_wait_event(WM_EVENT_ID);  // Block until input
        
        struct input_event ev;
        while (input_pop_key_event(&ev)) {
            handle_keypress(ev.code, ev.value);
        }
        while (input_pop_mouse_event(&ev)) {
            handle_mouse_event(&ev);
        }
        
        render_frame();
    }
}
```

### Legacy Single-Queue Processing

```c
void process_all_events(void) {
    struct input_event ev;
    while (input_pop_event(&ev)) {
        switch (ev.type) {
            case INPUT_TYPE_KEY:
                handle_key(ev.code, ev.value);
                break;
            case INPUT_TYPE_REL:
            case INPUT_TYPE_ABS:
                handle_mouse_move(ev.code, ev.value);
                break;
        }
    }
}
```

### Click Detection

```c
int check_click_in_rect(int rx, int ry, int rw, int rh) {
    int x, y, btn;
    input_get_mouse_state(&x, &y, &btn);
    
    if (btn && x >= rx && x < rx + rw && 
        y >= ry && y < ry + rh) {
        return 1;  // Click inside rectangle
    }
    return 0;
}
```

## Cross-References

### Related Documentation
- [irq.c.md](irq.c.md) - IRQ protection used in locking
- [framebuffer.c.md](framebuffer.c.md) - Screen coordinates for input

### Related Source Files
- `kernel/input.c` - Implementation
- `kernel/input.h` - Public API
- `kernel/virtio.c` - VirtIO input driver (calls input_push_event)
- `kernel/wm.c` - Window manager (consumes events)
- `kernel/cursor.c` - Cursor rendering (uses mouse state)
- `kernel/sched.c` - Task wakeup system

### VirtIO Integration

VirtIO input driver flow:
```
VirtIO Input Device
    ↓
virtio_input_poll() reads virtqueue
    ↓
Parse Linux input_event structure
    ↓
input_push_event(type, code, value)
    ↓
Queue event + update state
    ↓
task_wake_event() notifies consumers
```

### Window Manager Integration

Window manager consumes events:
```
WM Task blocks on WM_EVENT_ID
    ↓
Input event arrives
    ↓
task_wake_event(WM_EVENT_ID)
    ↓
WM wakes, calls input_pop_key_event()
    ↓
Process keyboard input
    ↓
Call input_get_mouse_state() for cursor
    ↓
Render frame, repeat
```

## Thread Safety

### Protected Operations
- **input_push_event()**: Lock + IRQ disable
- **input_pop_key_event()**: Lock + IRQ disable
- **input_pop_mouse_event()**: Lock + IRQ disable
- **input_pop_event()**: Uses protected pop functions
- **input_get_mouse_state()**: IRQ disable

### Lock Granularity

**Fine-Grained**: Separate locks for key/mouse queues possible but not implemented

**Current**: Single lock protects all state (simpler, sufficient for single-core)

### Interrupt Safety

IRQ handlers can safely call `input_push_event()` because:
1. Function disables interrupts (nested disable OK)
2. Spinlock prevents concurrent access
3. Short critical section (no blocking operations)

### Reentrancy

Functions are **reentrant** via locking:
- Multiple tasks can call simultaneously
- Interrupt can preempt task
- Spinlock ensures mutual exclusion

## Performance Considerations

### Latency Analysis

**Event Submission** (`input_push_event()`):
- State update: 10-20 cycles
- Lock acquire: 20-50 cycles
- Queue insert: 20-30 cycles
- Lock release: 10-20 cycles
- Task wakeup: 50-100 cycles
- **Total**: ~110-220 cycles

**Event Retrieval** (`input_pop_key_event()`):
- Lock acquire: 20-50 cycles
- Queue check: 10 cycles
- Copy event: 20 cycles
- Lock release: 10-20 cycles
- **Total**: ~60-100 cycles

**State Query** (`input_get_mouse_state()`):
- IRQ save: 10 cycles
- Read state: 10 cycles
- IRQ restore: 10 cycles
- **Total**: ~30 cycles

### Bottlenecks

1. **Spinlock Contention**: On SMP, multiple cores may contend
2. **Queue Full**: Events dropped silently (no notification)
3. **Division in Scaling**: `(val * max) / 32768` requires division

### Optimization Opportunities

1. **Lock-Free Queue**: Single-producer-single-consumer can be lock-free
2. **Event Merging**: Coalesce consecutive move events
3. **Shift-Based Scaling**: Use power-of-2 screen sizes for shift instead of divide
4. **Batch Wakeup**: Wake tasks once per event batch, not per event

## Debugging

### Check Queue State

```c
void debug_queues(void) {
    extern int key_head, key_tail;
    extern int mouse_head, mouse_tail;
    
    uart_puts("Key queue: ");
    uart_putu((key_head - key_tail + EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE);
    uart_puts(" events\n");
    
    uart_puts("Mouse queue: ");
    uart_putu((mouse_head - mouse_tail + EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE);
    uart_puts(" events\n");
}
```

### Log Events

```c
void log_event(uint16_t type, uint16_t code, int32_t value) {
    uart_puts("Event: type=");
    uart_putu(type);
    uart_puts(" code=");
    uart_putu(code);
    uart_puts(" value=");
    uart_putu(value);
    uart_puts("\n");
}

// Call from input_push_event()
```

### Test Coordinate Scaling

```c
void test_scaling(void) {
    uart_puts("Scale test:\n");
    for (int i = 0; i <= 32767; i += 4096) {
        int x = scale_mouse(i, 800);
        uart_puts("  ");
        uart_putu(i);
        uart_puts(" -> ");
        uart_putu(x);
        uart_puts("\n");
    }
}
```

## Future Enhancements

Possible improvements:

1. **Multi-Button Support**: Track all mouse buttons (left, right, middle, extra)
2. **Scroll Wheel**: Handle REL_WHEEL and REL_HWHEEL events
3. **Event Filtering**: Dead zones, button debouncing, acceleration curves
4. **Gesture Recognition**: Detect swipes, pinches (for touchscreens)
5. **Multi-Device**: Support multiple keyboards/mice with device IDs
6. **Event Timestamps**: Add timestamp field for latency measurement
7. **Hotplug**: Dynamic device addition/removal
8. **Lock-Free Queues**: SPSC queues for lower latency
9. **Event Merging**: Coalesce mouse moves to reduce queue pressure
10. **Input Remapping**: Key/button remapping table
11. **Keyboard State**: Track modifier keys (Shift, Ctrl, Alt)
12. **Key Repeat**: Automatic key repeat with configurable rate/delay
13. **Relative Acceleration**: Mouse acceleration curves
14. **Tablet Pressure**: Support pen pressure/tilt (ABS_PRESSURE, etc.)
15. **Touch Multi-Point**: Multi-touch gesture support

## Event Type Reference

### Complete Event Type List

| Type | Linux Name | Description |
|------|------------|-------------|
| 0x00 | EV_SYN | Synchronization markers |
| 0x01 | EV_KEY | Keys and buttons |
| 0x02 | EV_REL | Relative axes |
| 0x03 | EV_ABS | Absolute axes |
| 0x04 | EV_MSC | Miscellaneous |
| 0x05 | EV_SW | Switch events |
| 0x11 | EV_LED | LED control |
| 0x12 | EV_SND | Sound output |
| 0x14 | EV_REP | Key repeat |
| 0x15 | EV_FF | Force feedback |

**MYRASPOS Implements**: Types 0x01-0x04 only

### Key Code Examples

| Code | Name | Description |
|------|------|-------------|
| 0x01 | KEY_ESC | Escape key |
| 0x0E | KEY_BACKSPACE | Backspace |
| 0x1C | KEY_ENTER | Enter/Return |
| 0x39 | KEY_SPACE | Spacebar |
| 0x1E | KEY_A | Letter A |
| 0x110 | BTN_LEFT | Mouse left button |
| 0x111 | BTN_RIGHT | Mouse right button |
| 0x112 | BTN_MIDDLE | Mouse middle button |

**Full Keymap**: See Linux input-event-codes.h for complete reference
