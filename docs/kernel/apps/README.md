# MYRASPOS Applications Documentation

## Overview

MYRASPOS includes 7 graphical applications that run in the window manager environment. Each application is a separate task with its own window, event handling, and rendering logic.

**Location**: `kernel/apps/*.c`

## Application Architecture

All applications follow a common pattern:

```c
void app_start(void) {
    1. Create window via wm_create_window()
    2. Set render callback
    3. Register event handlers (keyboard, mouse)
    4. Enter event loop or yield control
}
```

### Window Integration
- Each app gets a dedicated window
- Window manager handles focus, composition, events
- Apps render to window-relative coordinates

### Event Handling
- Keyboard events: Character input, special keys
- Mouse events: Position, button clicks
- Window events: Close, resize, focus change

## Applications

### 1. Calculator App
**Files**: `kernel/apps/calculator_app.c`, `calculator_app.h`

**Purpose**: Basic arithmetic calculator with GUI

**Features**:
- Basic operations: +, -, ×, ÷
- Number buttons 0-9
- Decimal point support
- Clear and equals buttons
- Result display

**UI Layout**:
```
┌──────────────────────┐
│  Calculator          │ ← Title bar
├──────────────────────┤
│  [    0        ]     │ ← Display
├──────────────────────┤
│  [7] [8] [9] [÷]     │ ← Number pad
│  [4] [5] [6] [×]     │   and operators
│  [1] [2] [3] [-]     │
│  [0] [.] [=] [+]     │
│  [   Clear    ]      │
└──────────────────────┘
```

**Implementation Details**:
```c
struct calc_state {
    double display_value;
    double operand;
    char operation;  // '+', '-', '*', '/'
    int entering_new;
};

void calculator_render(struct window *win) {
    // Draw display
    wm_draw_rect(win, 10, 10, win->w - 20, 40, 0xFFFFFF);
    char display[32];
    snprintf(display, sizeof(display), "%.2f", state.display_value);
    wm_draw_text(win, 15, 20, display, 0x000000, 2);
    
    // Draw buttons in grid layout
    const char *buttons[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+"
    };
    
    for (int i = 0; i < 16; i++) {
        int x = (i % 4) * 60 + 10;
        int y = (i / 4) * 50 + 60;
        draw_button(win, x, y, 50, 40, buttons[i]);
    }
}
```

**Design Decisions**:
- **Double precision**: Uses `double` for all calculations
- **Button-based UI**: Click to enter (no keyboard input)
- **Simple state machine**: Tracks current operand, operation, display

**Constraints**:
- No scientific functions (sin, cos, etc.)
- No memory buttons (M+, M-, MR)
- Limited to basic arithmetic
- No expression parsing (immediate execution)

**Launch**: 
```c
void calculator_app_start(void);
```

Called from myra launcher or shell.

---

### 2. Editor App
**Files**: `kernel/apps/editor_app.c`, `editor_app.h`

**Purpose**: Simple text editor for creating and modifying text files

**Features**:
- File loading and saving
- Cursor navigation (arrow keys)
- Text insertion and deletion
- Line wrapping
- Scroll support for large files

**UI Layout**:
```
┌──────────────────────────────┐
│  Editor - filename.txt       │ ← Title bar
├──────────────────────────────┤
│  Line 1: Hello World         │ ← Text area
│  Line 2: This is a test      │   with cursor
│  Line 3: █                   │
│  ...                         │
├──────────────────────────────┤
│  Ctrl+S: Save | Ctrl+Q: Quit │ ← Status bar
└──────────────────────────────┘
```

**Implementation Details**:
```c
struct editor_state {
    char *buffer;           // Text buffer
    size_t buffer_size;
    size_t cursor_pos;      // Current cursor position
    int scroll_offset;      // Vertical scroll
    char filename[256];
    int modified;           // Dirty flag
};

void editor_render(struct window *win) {
    // Render visible lines
    int y = 10;
    int line_height = 16;
    int visible_lines = (win->h - 40) / line_height;
    
    char *line_start = buffer;
    int line_num = 0;
    
    while (*line_start && line_num < scroll_offset + visible_lines) {
        if (line_num >= scroll_offset) {
            // Find line end
            char *line_end = strchr(line_start, '\n');
            if (!line_end) line_end = line_start + strlen(line_start);
            
            // Draw line
            int len = line_end - line_start;
            char line[256];
            strncpy(line, line_start, len);
            line[len] = '\0';
            
            wm_draw_text(win, 10, y, line, 0x000000, 1);
            y += line_height;
        }
        
        line_start = line_end + 1;
        line_num++;
    }
    
    // Draw cursor
    int cursor_x, cursor_y;
    calculate_cursor_position(&cursor_x, &cursor_y);
    wm_draw_rect(win, cursor_x, cursor_y, 2, line_height, 0x000000);
}
```

**Key Commands**:
- **Arrow keys**: Move cursor
- **Backspace**: Delete character before cursor
- **Delete**: Delete character at cursor
- **Enter**: Insert newline
- **Ctrl+S**: Save file
- **Ctrl+Q**: Quit (prompts if unsaved)
- **Ctrl+X**: Cut
- **Ctrl+C**: Copy
- **Ctrl+V**: Paste

**Design Decisions**:
- **Gap buffer**: Efficient for text editing (future enhancement)
- **Current**: Simple flat buffer with insertion/deletion
- **Line-based rendering**: Renders visible lines only
- **No syntax highlighting**: Plain text only

**Constraints**:
- Maximum file size limited by heap memory
- No undo/redo (future enhancement)
- No search/replace
- No multiple buffers (single file at a time)
- ASCII only (no UTF-8)

**File Operations**:
```c
int editor_load_file(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    
    // Read entire file into buffer
    state.buffer_size = file_size(fd);
    state.buffer = kmalloc(state.buffer_size + 1);
    read(fd, state.buffer, state.buffer_size);
    state.buffer[state.buffer_size] = '\0';
    close(fd);
    
    strcpy(state.filename, filename);
    state.modified = 0;
    return 0;
}

int editor_save_file(void) {
    int fd = open(state.filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    
    write(fd, state.buffer, state.buffer_size);
    close(fd);
    
    state.modified = 0;
    return 0;
}
```

**Launch**:
```c
void editor_app_start(const char *filename);
```

---

### 3. Files App (File Manager)
**Files**: `kernel/apps/files_app.c`, `files_app.h`

**Purpose**: Graphical file manager for browsing and managing files

**Features**:
- Directory navigation
- File listing with icons
- File operations (copy, move, delete)
- Dual-pane view
- File preview

**UI Layout**:
```
┌────────────────────────────────────┐
│  Files - /home/user                │
├────────────────────────────────────┤
│  [↑ Parent] [Home] [Refresh]       │ ← Toolbar
├────────────────────────────────────┤
│  📁 Documents/                     │ ← File list
│  📁 Pictures/                      │   with icons
│  📄 readme.txt                     │
│  📄 notes.md                       │
│  🖼️ photo.png                      │
├────────────────────────────────────┤
│  5 items | 2 folders, 3 files      │ ← Status bar
└────────────────────────────────────┘
```

**Implementation**:
```c
struct files_state {
    char current_path[256];
    struct file_entry *entries;
    int entry_count;
    int selected_index;
    int scroll_offset;
};

struct file_entry {
    char name[64];
    int is_directory;
    size_t size;
    uint32_t icon_color;
};

void files_render(struct window *win) {
    // Draw toolbar
    draw_button(win, 10, 10, 80, 30, "Parent");
    draw_button(win, 100, 10, 80, 30, "Home");
    draw_button(win, 190, 10, 80, 30, "Refresh");
    
    // Draw file list
    int y = 50;
    int item_height = 30;
    
    for (int i = scroll_offset; i < entry_count; i++) {
        if (y > win->h - 40) break;
        
        struct file_entry *entry = &entries[i];
        
        // Highlight if selected
        if (i == selected_index) {
            wm_draw_rect(win, 0, y, win->w, item_height, 0xE0E0FF);
        }
        
        // Draw icon
        const char *icon = entry->is_directory ? "📁" : "📄";
        wm_draw_text(win, 10, y + 5, icon, entry->icon_color, 2);
        
        // Draw name
        wm_draw_text(win, 50, y + 5, entry->name, 0x000000, 1);
        
        // Draw size if file
        if (!entry->is_directory) {
            char size_str[32];
            snprintf(size_str, sizeof(size_str), "%zu B", entry->size);
            wm_draw_text(win, win->w - 100, y + 5, size_str, 0x808080, 1);
        }
        
        y += item_height;
    }
}
```

**Supported Operations**:
- **Double-click folder**: Navigate into folder
- **Double-click file**: Open with appropriate app
- **Right-click**: Context menu (copy, move, delete, rename)
- **Drag and drop**: Move files (future)

**Design Decisions**:
- **Icon-based view**: Visual file type indicators
- **Single selection**: One file/folder at a time
- **Modal dialogs**: For operations requiring confirmation

**Constraints**:
- No multi-selection
- No drag-and-drop (not implemented)
- No file search
- Limited to ramfs and diskfs

**Launch**:
```c
void files_app_start(void);
```

---

### 4. Image Viewer
**Files**: `kernel/apps/image_viewer.c`, `image_viewer.h`

**Purpose**: Display PNG images

**Features**:
- PNG image loading (via LodePNG)
- Zoom in/out
- Pan/scroll for large images
- Image information display

**UI Layout**:
```
┌────────────────────────────────────┐
│  Image Viewer - photo.png          │
├────────────────────────────────────┤
│  [←] [→] [+] [-] [Fit]             │ ← Toolbar
├────────────────────────────────────┤
│                                    │
│         [  Image  ]                │ ← Image area
│                                    │
│                                    │
├────────────────────────────────────┤
│  1280x720 | PNG | 2.4 MB           │ ← Status
└────────────────────────────────────┘
```

**Implementation**:
```c
struct image_viewer_state {
    uint32_t *image_data;
    int image_width;
    int image_height;
    float zoom;
    int pan_x, pan_y;
    char filename[256];
};

int image_viewer_load(const char *filename) {
    // Use LodePNG to decode
    unsigned char *png_data;
    size_t png_size;
    
    // Read PNG file
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    
    png_size = file_size(fd);
    png_data = kmalloc(png_size);
    read(fd, png_data, png_size);
    close(fd);
    
    // Decode PNG
    unsigned error = lodepng_decode32(
        &state.image_data,
        &state.image_width,
        &state.image_height,
        png_data,
        png_size
    );
    
    kfree(png_data);
    
    if (error) {
        printf("PNG decode error %u: %s\n", error, lodepng_error_text(error));
        return -1;
    }
    
    state.zoom = 1.0f;
    state.pan_x = state.pan_y = 0;
    return 0;
}

void image_viewer_render(struct window *win) {
    // Calculate visible region
    int display_w = (int)(state.image_width * state.zoom);
    int display_h = (int)(state.image_height * state.zoom);
    
    int x = (win->w - display_w) / 2 + state.pan_x;
    int y = (win->h - display_h) / 2 + state.pan_y;
    
    // Clip to window bounds
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    // Draw image (scaled if zoomed)
    wm_draw_bitmap(win, x, y, display_w, display_h,
                   state.image_data, 
                   state.image_width, state.image_height);
}
```

**Zoom Levels**:
- 25%, 50%, 75%, 100%, 150%, 200%, 400%
- Fit to window (automatic)

**Design Decisions**:
- **PNG only**: Uses LodePNG library (no JPEG, GIF)
- **32-bit RGBA**: Full color support with alpha
- **Software scaling**: CPU-based (no GPU)

**Constraints**:
- Memory intensive (uncompressed bitmap)
- Large images may exceed available memory
- No image editing features
- No rotation/flip

**Launch**:
```c
void image_viewer_start(const char *filename);
```

---

### 5. Keyboard Tester App
**Files**: `kernel/apps/keyboard_tester_app.c`, `keyboard_tester_app.h`

**Purpose**: Test keyboard input and display key codes

**Features**:
- Real-time key press display
- Scancode and ASCII display
- Modifier key status (Shift, Ctrl, Alt)
- Key repeat test

**UI Layout**:
```
┌────────────────────────────────────┐
│  Keyboard Tester                   │
├────────────────────────────────────┤
│  Press any key...                  │
│                                    │
│  Last Key: 'A'                     │
│  Scancode: 0x1E                    │
│  ASCII: 65                         │
│                                    │
│  Modifiers:                        │
│    [✓] Shift                       │
│    [ ] Ctrl                        │
│    [ ] Alt                         │
├────────────────────────────────────┤
│  Key History:                      │
│    A B C D E F G H I J             │
└────────────────────────────────────┘
```

**Implementation**:
```c
struct kb_tester_state {
    char last_char;
    uint16_t last_scancode;
    uint8_t modifiers;
    char history[64];
    int history_pos;
};

void kb_tester_handle_key(struct window *win, struct wm_input_event *ev) {
    state.last_scancode = ev->code;
    state.last_char = (char)ev->value;
    state.modifiers = get_modifier_state();
    
    // Add to history
    if (ev->value >= 32 && ev->value < 127) {
        state.history[state.history_pos] = (char)ev->value;
        state.history_pos = (state.history_pos + 1) % 64;
    }
    
    wm_request_render(win);
}
```

**Design Purpose**:
- Debugging input system
- Testing keyboard drivers
- User verification of key mapping

**Launch**:
```c
void keyboard_tester_app_start(void);
```

---

### 6. Myra App (Application Launcher)
**Files**: `kernel/apps/myra_app.c`, `myra_app.h`

**Purpose**: Main application launcher and menu

**Features**:
- Grid of application icons
- Application launch on click
- System shortcuts
- Quick access to common tasks

**UI Layout**:
```
┌────────────────────────────────────┐
│  MYRA                              │
├────────────────────────────────────┤
│  ┌──────┐  ┌──────┐  ┌──────┐     │
│  │  🧮  │  │  📝  │  │  📁  │     │
│  │ Calc │  │ Edit │  │ Files│     │
│  └──────┘  └──────┘  └──────┘     │
│                                    │
│  ┌──────┐  ┌──────┐  ┌──────┐     │
│  │  🖼️  │  │  ⌨️  │  │  💻  │     │
│  │ Image│  │  KB  │  │ Term │     │
│  └──────┘  └──────┘  └──────┘     │
└────────────────────────────────────┘
```

**Available Apps**:
1. Calculator
2. Editor
3. Files (File Manager)
4. Image Viewer
5. Keyboard Tester
6. Terminal

**Implementation**:
```c
struct app_entry {
    const char *name;
    const char *icon;
    void (*launch_fn)(void);
};

static struct app_entry apps[] = {
    {"Calculator", "🧮", calculator_app_start},
    {"Editor", "📝", editor_app_start},
    {"Files", "📁", files_app_start},
    {"Images", "🖼️", image_viewer_start},
    {"Keyboard", "⌨️", keyboard_tester_app_start},
    {"Terminal", "💻", terminal_app_start},
    {NULL, NULL, NULL}
};

void myra_render(struct window *win) {
    int grid_x = 3;  // columns
    int grid_y = 2;  // rows
    int cell_w = (win->w - 40) / grid_x;
    int cell_h = (win->h - 60) / grid_y;
    
    for (int i = 0; apps[i].name; i++) {
        int x = (i % grid_x) * cell_w + 20;
        int y = (i / grid_x) * cell_h + 20;
        
        // Draw icon
        wm_draw_text(win, x + cell_w/2 - 20, y + 10, 
                     apps[i].icon, 0x000000, 4);
        
        // Draw name
        wm_draw_text(win, x + cell_w/2 - 30, y + 70,
                     apps[i].name, 0x000000, 1);
        
        // Draw clickable area
        draw_button_border(win, x, y, cell_w - 10, cell_h - 10);
    }
}

void myra_handle_click(struct window *win, int x, int y) {
    // Calculate which app was clicked
    int grid_x = 3;
    int cell_w = (win->w - 40) / grid_x;
    int cell_h = (win->h - 60) / 2;
    
    int col = (x - 20) / cell_w;
    int row = (y - 20) / cell_h;
    int index = row * grid_x + col;
    
    if (index >= 0 && apps[index].name) {
        // Launch app
        task_create(apps[index].launch_fn, NULL, apps[index].name);
    }
}
```

**Toggle Behavior**:
```c
void myra_app_toggle(void) {
    if (myra_window_exists()) {
        wm_close_window(myra_window);
    } else {
        myra_app_open();
    }
}
```

**Hotkey**: Typically bound to Super key or Ctrl+Space

**Design Decisions**:
- **Always available**: Can be toggled at any time
- **Grid layout**: Scalable to more apps
- **Task spawning**: Each app runs as separate task

---

### 7. Terminal App
**Files**: `kernel/apps/terminal_app.c`, `terminal_app.h`

**Purpose**: Terminal emulator with shell integration

**Features**:
- Full shell access in GUI window
- Command history
- Scrollback buffer
- ANSI color support (basic)
- Copy/paste

**UI Layout**:
```
┌────────────────────────────────────┐
│  Terminal                          │
├────────────────────────────────────┤
│  user@myraspos:~$ ls               │
│  Documents  Pictures  readme.txt   │
│  user@myraspos:~$ cat readme.txt   │
│  Welcome to MYRASPOS!              │
│  user@myraspos:~$ █                │
│                                    │
│  [scrollback buffer...]            │
└────────────────────────────────────┘
```

**Implementation**:
```c
struct terminal_state {
    struct pty *pty;        // Pseudo-terminal
    char *buffer;           // Scrollback buffer
    int buffer_size;
    int cursor_x, cursor_y;
    int scroll_offset;
    uint32_t fg_color, bg_color;
};

void terminal_render(struct window *win) {
    // Draw terminal buffer
    int line_height = 16;
    int char_width = 8;
    int visible_lines = (win->h - 40) / line_height;
    
    char *line_ptr = state.buffer;
    int line_num = 0;
    int y = 10;
    
    // Skip scrolled lines
    for (int i = 0; i < state.scroll_offset; i++) {
        line_ptr = strchr(line_ptr, '\n');
        if (!line_ptr) return;
        line_ptr++;
    }
    
    // Draw visible lines
    while (*line_ptr && line_num < visible_lines) {
        char *line_end = strchr(line_ptr, '\n');
        if (!line_end) line_end = line_ptr + strlen(line_ptr);
        
        int len = line_end - line_ptr;
        char line[256];
        strncpy(line, line_ptr, len);
        line[len] = '\0';
        
        wm_draw_text(win, 10, y, line, state.fg_color, 1);
        
        y += line_height;
        line_num++;
        
        if (*line_end == '\n') line_ptr = line_end + 1;
        else break;
    }
    
    // Draw cursor
    int cursor_screen_x = 10 + state.cursor_x * char_width;
    int cursor_screen_y = y - line_height;
    wm_draw_rect(win, cursor_screen_x, cursor_screen_y, char_width, line_height, 0xFFFF00);
}
```

**PTY Integration**:
```c
void terminal_init(struct window *win) {
    // Create PTY
    state.pty = pty_create();
    
    // Start shell task on PTY
    task_create_with_pty(shell_main, state.pty, "shell", 16);
    
    // Redirect terminal window input to PTY
    win->user_data = state.pty;
}

void terminal_handle_key(struct window *win, char c) {
    // Send to PTY
    pty_write(state.pty, &c, 1);
}

void terminal_update(struct window *win) {
    // Read from PTY output
    char buf[256];
    int n = pty_read(state.pty, buf, sizeof(buf));
    
    if (n > 0) {
        // Append to buffer
        append_to_terminal_buffer(buf, n);
        wm_request_render(win);
    }
}
```

**Design Decisions**:
- **PTY-based**: Uses pseudo-terminal for shell I/O
- **Separate task**: Shell runs as independent task
- **Buffered output**: Maintains scrollback history

**Constraints**:
- Limited scrollback (e.g., 1000 lines)
- Basic ANSI support (colors only, no complex sequences)
- No tabs (converted to spaces)
- ASCII only

**Launch**:
```c
void terminal_app_start(void);
```

---

## Application Management

### Task Creation
All apps are created as separate tasks:

```c
void launch_app(void (*app_fn)(void), const char *name) {
    int tid = task_create(app_fn, NULL, name);
    if (tid < 0) {
        printf("Failed to launch %s\n", name);
    }
}
```

### Window Lifecycle
1. **Create**: App creates window via `wm_create_window()`
2. **Register callbacks**: Set render, close, event handlers
3. **Event loop**: Process events via window manager
4. **Close**: User closes window or app exits
5. **Cleanup**: Window manager frees window, task exits

### Memory Management
- Each app allocates its own state
- Window manager handles window memory
- Apps must free resources on exit

## Common Patterns

### App Structure Template
```c
struct app_state {
    struct window *win;
    // App-specific state
};

static struct app_state state;

void app_render(struct window *win) {
    // Rendering code
}

void app_handle_key(struct window *win, struct wm_input_event *ev) {
    // Keyboard handling
}

void app_handle_mouse(struct window *win, int x, int y, int buttons) {
    // Mouse handling
}

void app_start(void) {
    // Initialize state
    memset(&state, 0, sizeof(state));
    
    // Create window
    state.win = wm_create_window("App Name", 100, 100, 640, 480, app_render);
    if (!state.win) return;
    
    // Register handlers
    state.win->on_close = app_cleanup;
    state.win->user_data = &state;
    
    // Enter event loop
    while (state.win) {
        struct wm_input_event ev;
        if (wm_pop_key_event(state.win, &ev)) {
            app_handle_key(state.win, &ev);
        }
        yield();
    }
}
```

## Design Constraints

### Global Constraints
1. **Single instance**: Most apps don't prevent multiple instances
2. **Window manager dependency**: All apps require WM to be running
3. **Memory limits**: Large apps (editor, image viewer) limited by heap
4. **No persistence**: App state lost on close
5. **No inter-app communication**: Apps are isolated

### Performance Constraints
- **Rendering**: Software rendering only (no GPU)
- **Event latency**: Dependent on task scheduling frequency
- **Memory**: Each window allocates framebuffer memory

## Future Enhancements

### Planned
- Settings app (system configuration)
- Music player (audio support needed)
- Web browser (networking needed)
- Games (performance improvements needed)

### Not Planned
- Complex IDE features (too large)
- Video playback (no codec support)
- 3D applications (no 3D API)

## See Also
- [Window Manager](../wm/README.md)
- [Task Scheduling](../03-TASK-SCHEDULING.md)
- [Input System](../io/input.c.md)
- [Framebuffer](../io/framebuffer.c.md)
