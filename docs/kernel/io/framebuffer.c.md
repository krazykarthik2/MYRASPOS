# Framebuffer Graphics Subsystem Documentation (framebuffer.c/h)

## Overview

The framebuffer subsystem provides 2D graphics capabilities for MYRASPOS, enabling pixel manipulation, primitive drawing, text rendering, and bitmap operations. It interfaces with a memory-mapped framebuffer provided by VirtIO GPU or similar graphics hardware, supporting RGBA32 pixel format.

**Hardware Interface**: Memory-mapped framebuffer (VirtIO GPU)  
**Pixel Format**: 32-bit RGBA (0xAARRGGBB)  
**Typical Resolution**: 800×600 pixels (configurable)  
**Memory Layout**: Linear row-major format with stride

## Hardware Details

### Framebuffer Memory Layout

```
Physical Memory:
[Row 0 Pixels]..................[Padding]
[Row 1 Pixels]..................[Padding]
[Row 2 Pixels]..................[Padding]
...
[Row height-1 Pixels]...........[Padding]
```

**Linear Address**: `base + (y * stride * 4) + (x * 4)`  
**Stride**: Width in pixels (may include padding)  
**Pixel Size**: 4 bytes (32-bit RGBA)

### Pixel Format

**32-bit RGBA** (little-endian):

```
Bits [31:24]: Alpha (0xFF = opaque, 0x00 = transparent)
Bits [23:16]: Red
Bits [15:8]:  Green
Bits [7:0]:   Blue
```

**Example Colors**:
- `0xFF000000`: Black (opaque)
- `0xFFFFFFFF`: White (opaque)
- `0xFFFF0000`: Red (opaque)
- `0xFF00FF00`: Green (opaque)
- `0xFF0000FF`: Blue (opaque)
- `0x80FF0000`: Semi-transparent red (50% alpha)

### Framebuffer State

```c
static volatile uint32_t *fb = NULL;     // Framebuffer base address
static int fb_w = 0;                     // Width in pixels
static int fb_h = 0;                     // Height in pixels
static int fb_stride = 0;                // Stride in pixels (not bytes)
static int fb_init_done = 0;             // Initialization flag
```

**Volatile Qualifier**: Prevents compiler optimization of framebuffer writes (hardware may scan buffer)

## Key Functions

### Initialization

#### `void fb_init(void *addr, int width, int height, int stride_bytes)`
**Purpose**: Initialize framebuffer subsystem  
**Signature**: `void fb_init(void *addr, int width, int height, int stride_bytes)`  
**Parameters**:
- `addr` - Physical address of framebuffer memory
- `width` - Width in pixels
- `height` - Height in pixels
- `stride_bytes` - Bytes per row (includes padding)

**Behavior**:
1. Store framebuffer parameters (convert stride from bytes to pixels)
2. Clear framebuffer to black
3. Draw 50×50 white square in top-left as probe
4. Set initialization flag

**Implementation**:
```c
void fb_init(void *addr, int width, int height, int stride_bytes) {
    fb = (volatile uint32_t *)addr;
    fb_w = width;
    fb_h = height;
    fb_stride = stride_bytes / 4;  // Convert bytes to pixels
    fb_fill(0x000000);
    
    /* Draw probe square */
    for (int j=0; j<50; j++)
        for (int i=0; i<50; i++)
            fb[j*fb_stride + i] = 0xFFFFFF;
    
    fb_init_done = 1;
}
```

**Why Probe Square?**: Visual confirmation framebuffer is working and correctly mapped.

#### `int fb_is_init(void)`
**Purpose**: Check if framebuffer initialized  
**Returns**: 1 if initialized, 0 otherwise

#### `void fb_get_res(int *w, int *h)`
**Purpose**: Get framebuffer resolution  
**Parameters**:
- `w` - Pointer to store width (may be NULL)
- `h` - Pointer to store height (may be NULL)

### Pixel Operations

#### `void fb_set_pixel(int x, int y, uint32_t color)`
**Purpose**: Set single pixel color  
**Signature**: `void fb_set_pixel(int x, int y, uint32_t color)`  
**Parameters**:
- `x`, `y` - Pixel coordinates
- `color` - 32-bit RGBA color

**Behavior**:
- Bounds check: only write if (x,y) within framebuffer
- Calculate linear address: `fb[y * fb_stride + x]`
- Write color value

**Performance**: ~5-10 cycles (uncached), 1-2 cycles (cached)

#### `uint32_t fb_get_pixel(int x, int y)`
**Purpose**: Read pixel color  
**Returns**: 32-bit RGBA color, or 0 if out of bounds

### Fill Operations

#### `void fb_fill(uint32_t color)`
**Purpose**: Fill entire framebuffer with solid color  
**Signature**: `void fb_fill(uint32_t color)`  
**Parameters**:
- `color` - Fill color

**Algorithm**:
- Iterate through all rows
- For each row, write `fb_w` pixels
- Call `virtio_gpu_flush()` to update display

**Performance**: Optimized with pointer increment, ~1-2 cycles per pixel

**Implementation**:
```c
void fb_fill(uint32_t color) {
    if (!fb) return;
    for (int y = 0; y < fb_h; ++y) {
        volatile uint32_t *p = fb + (y * fb_stride);
        int n = fb_w;
        while (n--) *p++ = color;
    }
    virtio_gpu_flush();
}
```

### Rectangle Drawing

#### `void fb_draw_rect(int x, int y, int w, int h, uint32_t color)`
**Purpose**: Draw filled rectangle  
**Signature**: `void fb_draw_rect(int x, int y, int w, int h, uint32_t color)`  
**Parameters**:
- `x`, `y` - Top-left corner
- `w`, `h` - Width and height
- `color` - Fill color

**Features**:
- Automatic clipping to screen bounds
- Optimized row-at-a-time filling
- Returns early if clipped to zero size

**Clipping Logic**:
```c
if (x < 0) { w += x; x = 0; }          // Clip left
if (y < 0) { h += y; y = 0; }          // Clip top
if (x + w > fb_w) w = fb_w - x;        // Clip right
if (y + h > fb_h) h = fb_h - y;        // Clip bottom
if (w <= 0 || h <= 0) return;          // Empty after clipping
```

#### `void fb_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness)`
**Purpose**: Draw rectangle outline  
**Parameters**:
- `thickness` - Border thickness in pixels

**Implementation**: Four `fb_draw_rect()` calls for top/bottom/left/right edges

### Line Drawing

#### `void fb_draw_hline(int x1, int x2, int y, uint32_t color)`
**Purpose**: Draw horizontal line  
**Parameters**:
- `x1`, `x2` - Start and end X coordinates
- `y` - Y coordinate
- `color` - Line color

**Auto-swap**: If `x1 > x2`, swaps them for correct drawing

#### `void fb_draw_vline(int x, int y1, int y2, uint32_t color)`
**Purpose**: Draw vertical line  
**Parameters**: Similar to `fb_draw_hline()` but vertical

### Text Rendering

#### `void fb_draw_text(int x, int y, const char *s, uint32_t color, int scale)`
**Purpose**: Draw text string with scalable font  
**Signature**: `void fb_draw_text(int x, int y, const char *s, uint32_t color, int scale)`  
**Parameters**:
- `x`, `y` - Top-left position
- `s` - Null-terminated string
- `color` - Text color
- `scale` - Scale factor (1=5×7 pixels, 2=10×14 pixels, etc.)

**Font**: Built-in 5×7 pixel glyphs for ASCII characters  
**Character Set**: A-Z, a-z, 0-9, punctuation (~90 characters)  
**Spacing**: 1 pixel between characters (scaled with scale factor)

**Features**:
- Glyph cache (256 entries) for performance
- IRQ-protected cache access (thread-safe)
- Transparent background (only draws foreground pixels)
- LRU cache replacement

**Performance Optimization**:
```c
#define GLYPH_CACHE_SIZE 256
static uint32_t glyph_cache[GLYPH_CACHE_SIZE][5 * 7];  // Pre-rendered glyphs
static char glyph_cache_chars[GLYPH_CACHE_SIZE];
static uint32_t glyph_cache_colors[GLYPH_CACHE_SIZE];
```

**Cache Hit**: O(1) glyph lookup and render  
**Cache Miss**: Render glyph, add to cache, replace oldest

#### `void fb_draw_scaled_glyph(const uint8_t *g, int x, int y, int scale, uint32_t color)`
**Purpose**: Draw single glyph from raw data  
**Parameters**:
- `g` - Pointer to 7-byte glyph data (5 bits per row)
- `x`, `y` - Position
- `scale` - Scale factor
- `color` - Foreground color

**Glyph Format**:
```
Each byte represents one row of 5 pixels:
Bit 4 (MSB): Leftmost pixel
Bit 3: Second pixel
Bit 2: Middle pixel
Bit 1: Fourth pixel
Bit 0 (LSB): Rightmost pixel

Example 'A':
{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}
= {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}

 ###    (row 0)
#   #   (row 1)
#   #   (row 2)
#####   (row 3)
#   #   (row 4)
#   #   (row 5)
#   #   (row 6)
```

#### `void fb_put_text_centered(const char *s, uint32_t color)`
**Purpose**: Draw large centered text (splash screen)  
**Features**:
- Fixed scale 8 (40×56 pixels per character)
- Auto-centers horizontally and vertically
- Calls `virtio_gpu_flush()` to update display

**Use Case**: Boot splash screens, large messages

### Terminal Emulation

#### `void fb_puts(const char *s)`
**Purpose**: Simple terminal output with auto-scroll  
**Signature**: `void fb_puts(const char *s)`  
**Parameters**:
- `s` - Null-terminated string to display

**Features**:
- Scale 2 glyphs (10×14 pixels)
- 70 columns × 35 rows (fits 800×600)
- Auto-scroll: clear screen when bottom reached
- Newline support (`\n`)
- Control character filtering (skips < 32)

**Terminal State**:
```c
static int term_x = 0, term_y = 0;              // Cursor position
static const int term_scale = 2;                 // Character scale
static const int term_cols = 70;                 // Columns
static const int term_rows = 35;                 // Rows
```

**Character Spacing**: 11×15 pixels (5*2+1 × 7*2+1)

### Bitmap Drawing

#### `void fb_draw_bitmap_scaled(int x, int y, int w, int h, const uint32_t *bitmap, int bw, int bh, int cx, int cy, int cw, int ch)`
**Purpose**: Draw scaled bitmap with clipping and alpha blending  
**Signature**: Complex, see below  
**Parameters**:
- `x`, `y`, `w`, `h` - Destination rectangle (screen coordinates)
- `bitmap` - Source bitmap data
- `bw`, `bh` - Source bitmap dimensions
- `cx`, `cy`, `cw`, `ch` - Clip rectangle (screen coordinates)

**Features**:
- **Nearest-Neighbor Scaling**: Fast integer scaling algorithm
- **Alpha Blending**: Supports transparent/translucent pixels
- **Clipping**: Clips to specified rectangle
- **Optimized**: Per-row source pointer calculation

**Scaling Formula**:
```
src_x = (dst_x - x) * bw / w
src_y = (dst_y - y) * bh / h
```

**Alpha Blending**:
```c
if (alpha == 0) continue;           // Fully transparent
if (alpha == 255) {                 // Fully opaque
    dst = src;
} else {                            // Translucent
    r = (src_r * alpha + dst_r * (255 - alpha)) / 255;
    g = (src_g * alpha + dst_g * (255 - alpha)) / 255;
    b = (src_b * alpha + dst_b * (255 - alpha)) / 255;
    dst = 0xFF000000 | (r << 16) | (g << 8) | b;
}
```

**Use Cases**: Window manager icons, cursor, images

## Implementation Details

### Glyph Data Structure

```c
struct glyph5x7 {
    char ch;          // Character
    uint8_t rows[7];  // 7 rows of 5-bit pixel data
};

static const struct glyph5x7 glyphs[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    // ... 90+ glyphs
};
```

**Lookup Function**:
```c
static const uint8_t *get_glyph(char c) {
    for (size_t i = 0; i < sizeof(glyphs)/sizeof(glyphs[0]); ++i) {
        if (glyphs[i].ch == c) return glyphs[i].rows;
    }
    return glyphs[50].rows;  // Fallback to space
}
```

**Performance**: O(n) lookup, but cached after first use

### Glyph Cache Design

**Cache Structure**:
```c
static uint32_t glyph_cache[256][35];       // 5*7 pixels per glyph
static char glyph_cache_chars[256];         // Character
static uint32_t glyph_cache_colors[256];    // Color
static int glyph_cache_next = 0;            // Next slot (round-robin)
```

**Cache Lookup**:
1. Search cache for (character, color) pair
2. If found, use cached pixels
3. If not found, render to cache[next], increment next

**Why Cache Color?**: Same character in different colors needs different cache entries (pre-colored pixels)

**Thread Safety**: Cache access protected by `irq_save()`/`irq_restore()`

### VirtIO GPU Integration

**Flush Function**: `void virtio_gpu_flush(void)` (external)

**Purpose**: Notify VirtIO GPU to update display from framebuffer

**When to Call**:
- After `fb_fill()`
- After `fb_put_text_centered()`
- After terminal updates (`fb_puts()`)
- Window manager compositing

**Why Needed?**: VirtIO GPU uses two buffers:
- **Host Buffer**: Displayed on screen
- **Guest Buffer**: Framebuffer memory

Flush copies guest → host and triggers display update.

### Stride vs. Width

**Width**: Actual visible pixels per row  
**Stride**: Total memory per row (may include padding)

**Example**:
- Width = 800 pixels
- Stride = 1024 pixels (256-byte alignment)
- Padding = 224 pixels (896 bytes) per row

**Why Stride?**: Hardware may require power-of-2 or aligned row sizes

**Conversion**: `fb_stride = stride_bytes / 4` (bytes to pixels)

### Performance Optimizations

**1. Pointer Arithmetic**:
```c
volatile uint32_t *p = fb + (y * fb_stride) + x;
*p++ = color;  // Faster than array indexing
```

**2. Row-at-a-Time Fill**:
```c
volatile uint32_t *p = fb + (y * fb_stride);
int n = fb_w;
while (n--) *p++ = color;  // More efficient than per-pixel
```

**3. Glyph Cache**: Avoids re-rendering same characters

**4. Early Returns**: Bounds checks prevent unnecessary work

## Design Decisions

### Why 32-bit RGBA?

**Advantages**:
- Standard format, hardware-accelerated
- Alpha channel for transparency
- Direct color specification (no palette)
- Fast memcpy (32-bit aligned)

**Trade-offs**:
- 4× memory vs 8-bit palette (but RAM cheap)
- Wasted alpha for opaque graphics

### Why 5×7 Font?

**Advantages**:
- Compact (35 bits per glyph)
- Readable at scale 1 and above
- Fixed-width simplifies layout
- Easy to define in code

**Trade-offs**:
- Limited character detail
- No antialiasing

### Why Glyph Cache?

**Motivation**: Text rendering is performance-critical for:
- Terminal output
- Window titles
- UI labels

**Without Cache**: Each character requires:
1. Lookup glyph data (O(n))
2. Render 5×7 pixels
3. Scale to destination

**With Cache**: Cache hit requires only:
1. Lookup cached glyph (O(n), but n=256)
2. Blit 5×7 pre-colored pixels

**Speedup**: ~2-5× for repeated text rendering

### Why Volatile Framebuffer Pointer?

Framebuffer is hardware memory that:
- May be scanned by display controller
- Could be modified by DMA
- Needs consistent write ordering

`volatile` ensures compiler:
- Doesn't cache reads
- Doesn't eliminate writes
- Preserves access order

### Why Nearest-Neighbor Scaling?

**Alternatives**: Bilinear, bicubic interpolation

**Nearest-Neighbor Advantages**:
- Fast: integer division only
- Simple: no floating point
- Acceptable for UI graphics
- Preserves hard edges (icons)

**Trade-off**: Pixelated appearance at non-integer scales

## Constraints

### Hardware Constraints

- **Memory-Mapped**: Assumes linear framebuffer in physical memory
- **RGBA32 Only**: No support for other pixel formats
- **Single Buffer**: No double-buffering (handled by VirtIO)
- **Fixed Format**: No palette modes or indexed color

### Software Constraints

- **No Clipping Context**: Each function clips independently
- **No Transforms**: No rotation, shear, or perspective
- **Limited Fonts**: Single 5×7 font only
- **No Antialiasing**: Aliased text and graphics

### Performance Constraints

- **Glyph Lookup**: O(n) for 90+ glyphs (could use hash table)
- **Alpha Blend**: Requires read-modify-write (slow for large bitmaps)
- **No Acceleration**: All operations software-rendered
- **Cache Size**: 256-entry glyph cache may thrash with many colors

## Usage Examples

### Clear Screen and Draw Rectangle

```c
#include "framebuffer.h"

void draw_ui(void) {
    fb_fill(0xFF000080);  // Dark blue background
    fb_draw_rect(100, 100, 200, 150, 0xFFFFFFFF);  // White rect
    fb_draw_rect_outline(100, 100, 200, 150, 0xFF000000, 2);  // Black border
}
```

### Draw Text

```c
void show_message(void) {
    fb_draw_text(50, 50, "Hello, MYRASPOS!", 0xFFFFFFFF, 2);  // Scale 2
    fb_draw_text(50, 80, "System Ready", 0xFF00FF00, 1);      // Scale 1
}
```

### Terminal Output

```c
void boot_messages(void) {
    fb_puts("MYRASPOS v1.0\n");
    fb_puts("Initializing...\n");
    fb_puts("System ready.\n");
}
```

### Splash Screen

```c
void show_splash(void) {
    fb_fill(0xFF000000);  // Black
    fb_put_text_centered("MYRASPOS", 0xFF00FFFF);  // Cyan, centered
}
```

### Draw Icon with Alpha

```c
void draw_icon(int x, int y) {
    extern const uint32_t icon_data[];  // 32×32 RGBA bitmap
    int screen_w, screen_h;
    fb_get_res(&screen_w, &screen_h);
    
    fb_draw_bitmap_scaled(
        x, y, 32, 32,           // Destination (no scaling)
        icon_data, 32, 32,      // Source
        0, 0, screen_w, screen_h // Clip to screen
    );
}
```

### Progress Bar

```c
void draw_progress(int percent) {
    int bar_w = 400, bar_h = 30;
    int x = 200, y = 300;
    
    fb_draw_rect(x, y, bar_w, bar_h, 0xFF404040);  // Background
    
    int fill_w = (bar_w * percent) / 100;
    fb_draw_rect(x, y, fill_w, bar_h, 0xFF00FF00);  // Progress (green)
    
    fb_draw_rect_outline(x, y, bar_w, bar_h, 0xFFFFFFFF, 2);  // Border
}
```

### Pixel-Level Drawing

```c
void draw_gradient(void) {
    int w, h;
    fb_get_res(&w, &h);
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r = (x * 255) / w;
            uint8_t g = (y * 255) / h;
            uint8_t b = 128;
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            fb_set_pixel(x, y, color);
        }
    }
    virtio_gpu_flush();
}
```

## Cross-References

### Related Documentation
- [input.c.md](input.c.md) - Input events for interactive graphics
- [irq.c.md](irq.c.md) - IRQ save/restore used in glyph cache

### Related Source Files
- `kernel/framebuffer.c` - Implementation
- `kernel/framebuffer.h` - Public API
- `kernel/virtio.c` - VirtIO GPU driver (flush function)
- `kernel/wm.c` - Window manager (uses framebuffer)
- `kernel/cursor.c` - Mouse cursor rendering

### Integration with Window Manager

Window manager uses framebuffer for:
- Window compositing
- Title bar rendering
- Icon drawing
- Cursor overlay

**Typical Flow**:
```
Window Manager Frame Update
    ↓
fb_fill(background)
    ↓
For each window:
    fb_draw_rect(window_content)
    fb_draw_rect_outline(border)
    fb_draw_text(title)
    ↓
draw_cursor(mouse_x, mouse_y)
    ↓
virtio_gpu_flush()
```

## Thread Safety

The framebuffer subsystem has **mixed thread safety**:

### Thread-Safe Operations
- **fb_draw_text()**: Glyph cache access protected by `irq_save()`/`irq_restore()`

### Unsafe Operations
- **All other functions**: No locking, assume single-threaded or external synchronization

### Shared State
- `term_x`, `term_y`: Terminal cursor (not protected)
- `glyph_cache`: Protected in `fb_draw_text()` only
- Framebuffer memory: No protection (hardware may read concurrently)

### Recommended Protection

For multi-threaded access:
```c
unsigned long flags = irq_save();
fb_draw_rect(x, y, w, h, color);
fb_draw_text(x2, y2, text, color, scale);
irq_restore(flags);
```

Or use higher-level lock (window manager serializes updates).

## Performance Benchmarks

**Typical Performance** (800×600, 62.5 MHz ARM timer):

| Operation | Cycles | Time (μs) | Notes |
|-----------|--------|-----------|-------|
| fb_set_pixel() | 10-50 | 0.16-0.8 | Cached/uncached |
| fb_fill() | 480K-960K | 7680-15360 | Full screen |
| fb_draw_rect() (100×100) | 10K-50K | 160-800 | Small rect |
| fb_draw_text() (cached) | 350-1750 | 5.6-28 | Per character |
| fb_draw_text() (uncached) | 700-3500 | 11.2-56 | First render |
| fb_draw_bitmap_scaled() | Varies | Varies | Depends on size/alpha |

**Bottlenecks**:
1. Memory bandwidth (uncached framebuffer)
2. Division in scaling (could optimize)
3. Alpha blending read-modify-write

## Future Enhancements

Possible improvements:

1. **Hardware Acceleration**: Use VirtIO GPU 3D commands
2. **Double Buffering**: Reduce tearing
3. **Font Support**: TrueType or bitmap fonts
4. **Antialiasing**: Subpixel rendering
5. **Line Drawing**: Bresenham algorithm for arbitrary lines
6. **Circle/Ellipse**: Primitive shapes
7. **Polygon Fill**: General polygon rasterization
8. **Image Formats**: PNG/JPEG decoding (via lodepng, already available)
9. **Clipping Context**: Save/restore clip rectangles
10. **Transforms**: Rotation, scaling matrices
11. **Optimized Blending**: SIMD alpha blending
12. **Dirty Rectangles**: Only flush changed regions
13. **Multiple Buffers**: Back buffer compositing
14. **Gamma Correction**: Color-accurate rendering
