# Window Manager Documentation

## Overview

The MYRASPOS window manager (WM) provides a complete windowing system with desktop composition, event routing, and application window management. It implements a stacking window model with focus management and input event distribution.

**Files**: 
- `kernel/wm.c` - Window manager implementation
- `kernel/wm.h` - Window manager API
- `kernel/cursor.c` - Cursor rendering and management
- `kernel/cursor.h` - Cursor API

## Architecture

### Core Components

```
┌─────────────────────────────────────────┐
│           Desktop Composition           │
│  ┌─────────────────────────────────┐   │
│  │     Wallpaper Layer             │   │
│  ├─────────────────────────────────┤   │
│  │     Window Layer (stacked)      │   │
│  │  ┌────────┐    ┌────────┐      │   │
│  │  │ Win 1  │    │ Win 2  │      │   │
│  │  └────────┘    └────────┘      │   │
│  ├─────────────────────────────────┤   │
│  │     Cursor Layer                │   │
│  ├─────────────────────────────────┤   │
│  │     Taskbar Layer               │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

### Data Structures

#### Window Structure
```c
struct window {
    // Identity
    int id;                             // Unique window ID
    char name[WM_WINDOW_NAME_MAX];      // Window title (32 chars)
    
    // Geometry
    int x, y, w, h;                     // Current position and size
    int saved_x, saved_y, saved_w, saved_h;  // For minimize/maximize
    wm_state_t state;                   // Window state
    
    // Appearance
    uint32_t border_color;              // Border color (RGBA)
    uint32_t title_color;               // Title bar color
    
    // Callbacks
    void (*render)(struct window *win);  // Render callback
    void (*on_close)(struct window *win); // Close callback
    
    // State
    void *user_data;                    // App-specific data
    struct window *next;                // Linked list
    struct pty *tty;                    // Associated PTY (if terminal)
    
    // Input queue
    struct wm_input_event input_queue[WM_INPUT_QUEUE_SIZE];
    int input_head, input_tail;
    volatile int input_lock;            // Spinlock for queue
    
    // Dirty flag
    int is_dirty;                       // Needs redraw
};
```

#### Window States
```c
typedef enum {
    WM_STATE_NORMAL,           // Regular window
    WM_STATE_MINIMIZED,        // Hidden/minimized
    WM_STATE_MAXIMIZED,        // Fullscreen (covers taskbar)
    WM_STATE_FULLSCREEN,       // True fullscreen
    WM_STATE_MAXIMIZED_TASKBAR // Maximized but taskbar visible
} wm_state_t;
```

#### Input Event
```c
struct wm_input_event {
    uint16_t type;    // Event type (KEY_PRESS, KEY_RELEASE, etc.)
    uint16_t code;    // Scancode or button
    int32_t value;    // ASCII value or coordinate
};
```

## Window Management

### Window Creation
```c
struct window* wm_create_window(
    const char *name,           // Window title
    int x, int y,              // Initial position
    int w, int h,              // Initial size
    void (*render_fn)(struct window*)  // Render callback
);
```

**Implementation**:
```c
struct window* wm_create_window(const char *name, int x, int y, int w, int h, 
                                void (*render_fn)(struct window*)) {
    wm_list_lock();
    
    struct window *win = kmalloc(sizeof(*win));
    if (!win) {
        wm_list_unlock();
        return NULL;
    }
    
    memset(win, 0, sizeof(*win));
    win->id = next_win_id++;
    strncpy(win->name, name, WM_WINDOW_NAME_MAX - 1);
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->state = WM_STATE_NORMAL;
    win->border_color = 0xFF404040;
    win->title_color = 0xFF606060;
    win->render = render_fn;
    win->is_dirty = 1;
    
    // Add to list (prepend for z-order)
    win->next = window_list;
    window_list = win;
    
    wm_list_unlock();
    
    // Focus new window
    wm_focus_window(win);
    desktop_dirty = 1;
    
    return win;
}
```

**Design Decisions**:
- **Prepend to list**: New windows appear on top
- **Auto-focus**: Newly created windows get focus
- **Default styling**: Consistent appearance across apps

### Window Closure
```c
void wm_close_window(struct window *win);
```

**Implementation**:
```c
void wm_close_window(struct window *win) {
    if (!win) return;
    
    // Call close callback (cleanup)
    if (win->on_close) {
        win->on_close(win);
    }
    
    wm_list_lock();
    
    // Remove from list
    if (window_list == win) {
        window_list = win->next;
    } else {
        struct window *prev = window_list;
        while (prev && prev->next != win) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = win->next;
        }
    }
    
    // Unfocus if focused
    if (focused_window == win) {
        focused_window = window_list;  // Focus next window
    }
    
    wm_list_unlock();
    
    kfree(win);
    desktop_dirty = 1;
}
```

**Design Decision**: Close callback for cleanup
- Apps can save state before window closes
- Free allocated resources
- Detach from services (PTY, etc.)

### Focus Management
```c
void wm_focus_window(struct window *win);
int wm_is_focused(struct window *win);
```

**Implementation**:
```c
void wm_focus_window(struct window *win) {
    if (!win || focused_window == win) return;
    
    wm_list_lock();
    
    // Move window to front of list (top of z-order)
    if (window_list != win) {
        struct window *prev = window_list;
        while (prev && prev->next != win) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = win->next;
            win->next = window_list;
            window_list = win;
        }
    }
    
    focused_window = win;
    desktop_dirty = 1;
    
    wm_list_unlock();
}
```

**Design Decision**: Click-to-focus
- Window brought to front on focus
- All keyboard input goes to focused window
- Visual indication (highlighted title bar)

### Window State Changes
```c
void wm_set_state(struct window *win, wm_state_t state);
```

**States**:
1. **NORMAL**: Standard window
2. **MINIMIZED**: Hidden (not rendered)
3. **MAXIMIZED_TASKBAR**: Fills screen except taskbar
4. **MAXIMIZED**: Fills entire screen (no taskbar)
5. **FULLSCREEN**: True fullscreen (no decorations)

**Implementation**:
```c
void wm_set_state(struct window *win, wm_state_t state) {
    if (!win || win->state == state) return;
    
    if (state == WM_STATE_MAXIMIZED_TASKBAR || 
        state == WM_STATE_MAXIMIZED) {
        // Save current geometry
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_w = win->w;
        win->saved_h = win->h;
        
        // Maximize
        win->x = 0;
        win->y = 0;
        win->w = screen_w;
        win->h = (state == WM_STATE_MAXIMIZED) ? screen_h : screen_h - taskbar_h;
    } else if (win->state == WM_STATE_MAXIMIZED_TASKBAR || 
               win->state == WM_STATE_MAXIMIZED) {
        // Restore geometry
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->w = win->saved_w;
        win->h = win->saved_h;
    }
    
    win->state = state;
    desktop_dirty = 1;
}
```

## Rendering System

### Composition Pipeline
```c
void wm_compose(void) {
    1. Clear/draw wallpaper
    2. Render windows (back to front)
    3. Draw window decorations (title bars, borders)
    4. Draw taskbar
    5. Draw cursor
    6. Flip framebuffer
}
```

**Implementation**:
```c
void wm_compose(void) {
    if (!desktop_dirty) return;
    
    // 1. Draw wallpaper or clear
    if (wallpaper_buf) {
        fb_draw_bitmap(0, 0, screen_w, screen_h, 
                      wallpaper_buf, wallpaper_w, wallpaper_h);
    } else {
        fb_fill_rect(0, 0, screen_w, screen_h, 0xFF2B579A);  // Blue bg
    }
    
    // 2. Render windows (reverse order = back to front)
    struct window *win = window_list;
    struct window *stack[64];
    int count = 0;
    
    // Build stack
    while (win && count < 64) {
        if (win->state != WM_STATE_MINIMIZED) {
            stack[count++] = win;
        }
        win = win->next;
    }
    
    // Render from back to front
    for (int i = count - 1; i >= 0; i--) {
        win = stack[i];
        
        // Draw window background
        fb_fill_rect(win->x, win->y, win->w, win->h, 0xFFFFFFFF);
        
        // Draw title bar
        uint32_t title_color = (win == focused_window) ? 
                               0xFF4080FF : win->title_color;
        fb_fill_rect(win->x, win->y, win->w, 24, title_color);
        fb_draw_text(win->x + 5, win->y + 5, win->name, 0xFFFFFFFF, 1);
        
        // Draw border
        fb_draw_rect(win->x, win->y, win->w, win->h, win->border_color);
        
        // Call app's render callback
        if (win->render) {
            // Set clipping region to window interior
            fb_set_clip(win->x, win->y + 24, win->w, win->h - 24);
            win->render(win);
            fb_clear_clip();
        }
        
        win->is_dirty = 0;
    }
    
    // 3. Draw taskbar
    draw_taskbar();
    
    // 4. Draw cursor
    int mx, my, btn;
    wm_get_mouse_state(&mx, &my, &btn);
    cursor_draw(mx, my);
    
    desktop_dirty = 0;
}
```

**Design Decisions**:
- **Immediate mode**: Full redraw every frame
- **Back-to-front**: Correct overlapping windows
- **Clipping**: Apps draw only in their window area
- **Dirty flags**: Skip compose if nothing changed

**Optimization Opportunity**: Damage rectangles (future)
- Track which regions changed
- Only redraw dirty regions
- Significant performance improvement

### Window-Relative Drawing
```c
void wm_draw_rect(struct window *win, int x, int y, int w, int h, uint32_t color);
void wm_draw_text(struct window *win, int x, int y, const char *text, 
                  uint32_t color, int scale);
void wm_draw_bitmap(struct window *win, int x, int y, int w, int h, 
                    const uint32_t *bitmap, int bw, int bh);
```

**Coordinate Translation**:
```c
void wm_draw_rect(struct window *win, int x, int y, int w, int h, uint32_t color) {
    // Translate to screen coordinates
    int screen_x = win->x + x;
    int screen_y = win->y + 24 + y;  // +24 for title bar
    
    // Clip to window bounds
    if (screen_x + w > win->x + win->w) {
        w = win->x + win->w - screen_x;
    }
    if (screen_y + h > win->y + win->h) {
        h = win->y + win->h - screen_y;
    }
    
    // Draw to framebuffer
    fb_fill_rect(screen_x, screen_y, w, h, color);
}
```

**Design Decision**: Apps use window-relative coordinates
- **Advantage**: Apps don't care about absolute position
- **Simplifies**: Window movement, maximization
- **Clipping**: Automatic by WM

## Input System

### Event Flow
```
Input Device (Keyboard/Mouse)
    ↓
Input Driver (kernel/input.c)
    ↓
Window Manager (route to focused window)
    ↓
Window Input Queue (per-window)
    ↓
Application (pop events)
```

### Keyboard Input
```c
void wm_handle_keyboard(uint16_t scancode, int pressed);
```

**Implementation**:
```c
void wm_handle_keyboard(uint16_t scancode, int pressed) {
    // Update modifier state
    if (scancode == 0x2A || scancode == 0x36) {  // Shift
        shift_state = pressed;
        return;
    }
    if (scancode == 0x3A && pressed) {  // Caps Lock
        caps_lock = !caps_lock;
        return;
    }
    
    if (!pressed) return;  // Only handle key presses
    
    // Global hotkeys
    if (scancode == 0x01) {  // ESC - toggle Myra
        myra_app_toggle();
        return;
    }
    
    // Route to focused window
    if (!focused_window) return;
    
    // Convert scancode to ASCII
    char ascii = 0;
    if (scancode < sizeof(scan_to_ascii)) {
        ascii = (shift_state || caps_lock) ? 
                scan_to_ascii_shift[scancode] : 
                scan_to_ascii[scancode];
    }
    
    // Push to window's input queue
    struct wm_input_event ev;
    ev.type = EV_KEY;
    ev.code = scancode;
    ev.value = ascii;
    
    wm_push_input_event(focused_window, &ev);
}
```

**Design Decisions**:
- **Scancode to ASCII**: Built-in translation tables
- **Modifier tracking**: Shift, Caps Lock handled by WM
- **Global hotkeys**: ESC for launcher (before routing)
- **Per-window queues**: Each window has independent input

### Mouse Input
```c
void wm_handle_mouse(int dx, int dy, int buttons);
```

**Implementation**:
```c
void wm_handle_mouse(int dx, int dy, int buttons) {
    // Update mouse position
    wm_last_mx += dx;
    wm_last_my += dy;
    
    // Clamp to screen
    if (wm_last_mx < 0) wm_last_mx = 0;
    if (wm_last_my < 0) wm_last_my = 0;
    if (wm_last_mx >= screen_w) wm_last_mx = screen_w - 1;
    if (wm_last_my >= screen_h) wm_last_my = screen_h - 1;
    
    // Check for window clicks
    if (buttons && !prev_buttons) {  // Button press
        struct window *clicked = find_window_at(wm_last_mx, wm_last_my);
        if (clicked && clicked != focused_window) {
            wm_focus_window(clicked);
        }
    }
    
    // Route to focused window
    if (focused_window) {
        struct wm_input_event ev;
        ev.type = EV_MOUSE;
        ev.code = buttons;
        
        // Translate to window coordinates
        int win_x = wm_last_mx - focused_window->x;
        int win_y = wm_last_my - focused_window->y - 24;  // Title bar
        ev.value = (win_x << 16) | (win_y & 0xFFFF);
        
        wm_push_input_event(focused_window, &ev);
    }
    
    prev_buttons = buttons;
    desktop_dirty = 1;  // Cursor moved
}
```

### Input Queue Management
```c
int wm_pop_key_event(struct window *win, struct wm_input_event *ev);
```

**Thread-Safe Queue**:
```c
void wm_push_input_event(struct window *win, struct wm_input_event *ev) {
    if (!win) return;
    
    wm_lock_window(win);
    
    int next_head = (win->input_head + 1) % WM_INPUT_QUEUE_SIZE;
    if (next_head != win->input_tail) {  // Not full
        win->input_queue[win->input_head] = *ev;
        win->input_head = next_head;
    }
    
    wm_unlock_window(win);
}

int wm_pop_key_event(struct window *win, struct wm_input_event *ev) {
    if (!win) return 0;
    
    wm_lock_window(win);
    
    if (win->input_head == win->input_tail) {  // Empty
        wm_unlock_window(win);
        return 0;
    }
    
    *ev = win->input_queue[win->input_tail];
    win->input_tail = (win->input_tail + 1) % WM_INPUT_QUEUE_SIZE;
    
    wm_unlock_window(win);
    return 1;
}
```

**Design Decision**: Circular buffer with spinlock
- **Thread-safe**: Apps and WM can access concurrently
- **Fixed size**: 128 events per window
- **Drop on overflow**: Prevents deadlock

## Taskbar

### Taskbar Layout
```
┌─────────────────────────────────────────┐
│ [📱] Calculator  Editor  Terminal  ...  │
└─────────────────────────────────────────┘
```

**Implementation**:
```c
void draw_taskbar(void) {
    int taskbar_y = screen_h - taskbar_h;
    
    // Draw taskbar background
    fb_fill_rect(0, taskbar_y, screen_w, taskbar_h, 0xFF404040);
    
    // Draw launcher icon
    fb_draw_text(5, taskbar_y + 8, "MYRA", 0xFFFFFFFF, 1);
    
    // Draw window buttons
    int x = 60;
    struct window *win = window_list;
    while (win) {
        if (win->state != WM_STATE_MINIMIZED) {
            uint32_t color = (win == focused_window) ? 0xFF4080FF : 0xFF606060;
            fb_fill_rect(x, taskbar_y + 4, 100, 24, color);
            fb_draw_text(x + 5, taskbar_y + 8, win->name, 0xFFFFFFFF, 1);
            x += 110;
        }
        win = win->next;
    }
}
```

**Design Decision**: Always visible
- **Purpose**: Quick app switching
- **Launcher access**: Myra button always available
- **Window list**: All open windows shown

## Cursor Management

**File**: `kernel/cursor.c`

### Cursor Rendering
```c
void cursor_draw(int x, int y);
```

**Implementation**:
```c
static const uint8_t cursor_bitmap[] = {
    // Arrow cursor (11x16 pixels)
    0xFF, 0x00, 0x00, 0x00, ...
    0xFF, 0xFF, 0x00, 0x00, ...
    // ... (bitmap data)
};

void cursor_draw(int x, int y) {
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 11; cx++) {
            if (cursor_bitmap[cy * 11 + cx]) {
                fb_put_pixel(x + cx, y + cy, 0xFFFFFFFF);
            }
        }
    }
}
```

**Design Decision**: Software cursor
- **Always on top**: Drawn last in composition
- **Custom shapes**: Easy to change cursor appearance
- **No hardware cursor**: Not available on VirtIO GPU

## Performance Considerations

### Bottlenecks
1. **Full screen redraws**: Every frame redraws everything
2. **No dirty rectangles**: Can't skip unchanged regions
3. **Software rendering**: No GPU acceleration

### Optimizations
1. **Dirty flag**: Skip compose if nothing changed
2. **Minimized windows**: Don't render hidden windows
3. **Clipping**: Don't draw outside window bounds

### Future Improvements
- **Damage tracking**: Only redraw changed regions
- **Double buffering**: Prevent tearing
- **Vsync**: Smooth 60 FPS rendering
- **Object pools**: Reduce allocation overhead

## Design Constraints

### Hard Constraints
1. **Single desktop**: No multiple workspaces
2. **Stacking only**: No tiling window mode
3. **Software rendering**: No hardware acceleration
4. **Fixed taskbar**: Always bottom, no hiding
5. **No transparency**: Alpha not fully implemented

### Soft Constraints
1. **Window limit**: Memory-limited (typically <50 windows)
2. **Input latency**: Depends on composition frequency
3. **Resolution**: Fixed at boot (no runtime change)

## See Also
- [Applications Documentation](../apps/README.md)
- [Input System](../io/input.c.md)
- [Framebuffer](../io/framebuffer.c.md)
- [Task Scheduling](../03-TASK-SCHEDULING.md)
