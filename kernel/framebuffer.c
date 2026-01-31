#include "framebuffer.h"
#include "virtio.h"
#include <stdint.h>
#include <stddef.h>

static volatile uint32_t *fb = NULL;
static int fb_w = 0;
static int fb_h = 0; 
static int fb_stride = 0; /* in pixels (not bytes) */
static int fb_init_done = 0;

/* Simple 5x7 block glyphs for a small character set used in the splash */
struct glyph5x7 { char ch; uint8_t rows[7]; };

static const struct glyph5x7 glyphs[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x13,0x11,0x11,0x0E}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}},
    {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0', {0x0E,0x11,0x19,0x15,0x13,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x1E,0x11,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x04,0x04,0x04}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x00,0x04}},
    {':', {0x00,0x04,0x00,0x00,0x00,0x04,0x00}},
    {'/', {0x01,0x02,0x04,0x08,0x10,0x20,0x00}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
    {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'$', {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'>', {0x10,0x08,0x04,0x02,0x04,0x08,0x10}},
    {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
    {'|', {0x04,0x04,0x04,0x00,0x04,0x04,0x04}},
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    /* Lowercase - using minimal 5x7 definitions */
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
    {'b', {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}},
    {'c', {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}},
    {'d', {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}},
    {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
    {'f', {0x06,0x09,0x1C,0x08,0x08,0x08,0x08}},
    {'g', {0x00,0x0F,0x11,0x0F,0x01,0x11,0x0E}},
    {'h', {0x10,0x10,0x16,0x19,0x11,0x11,0x11}},
    {'i', {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}},
    {'j', {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}},
    {'k', {0x10,0x10,0x11,0x12,0x1C,0x12,0x11}},
    {'l', {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'m', {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}},
    {'n', {0x00,0x00,0x16,0x19,0x11,0x11,0x11}},
    {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
    {'p', {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}},
    {'q', {0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}},
    {'r', {0x00,0x00,0x16,0x19,0x10,0x10,0x10}},
    {'s', {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}},
    {'t', {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}},
    {'u', {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}},
    {'v', {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}},
    {'w', {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}},
    {'x', {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}},
    {'y', {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}},
    {'z', {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}},
    {'@', {0x0E,0x11,0x17,0x15,0x1D,0x10,0x0F}},
    {'#', {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}},
    {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
    {'^', {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}},
    {'&', {0x0C,0x12,0x12,0x0C,0x15,0x12,0x0D}},
    {'*', {0x04,0x15,0x0E,0x15,0x04,0x00,0x00}},
    {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'{', {0x06,0x08,0x10,0x18,0x10,0x08,0x06}},
    {'}', {0x0C,0x02,0x01,0x03,0x01,0x02,0x0C}},
    {';', {0x00,0x04,0x00,0x00,0x04,0x04,0x08}},
    {'\'', {0x0C,0x04,0x08,0x00,0x00,0x00,0x00}},
    {'"', {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}},
    {'`', {0x08,0x04,0x02,0x00,0x00,0x00,0x00}},
    {'~', {0x00,0x00,0x0D,0x16,0x00,0x00,0x00}},
    {'\\', {0x10,0x08,0x04,0x02,0x01,0x00,0x00}},
    {',', {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
    {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
};

static const uint8_t *get_glyph(char c) {
    // if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); // Now we support lowercase
    for (size_t i = 0; i < sizeof(glyphs)/sizeof(glyphs[0]); ++i) {
        if (glyphs[i].ch == c) return glyphs[i].rows;
    }
    /* fallback: space (mapping ' ' to empty pixels) */
    return glyphs[50].rows;
}

void fb_init(void *addr, int width, int height, int stride_bytes) {
    fb = (volatile uint32_t *)addr;
    fb_w = width;
    fb_h = height;
    fb_stride = stride_bytes / 4;
    fb_fill(0x000000); /* Mandatory clear */
    /* Draw a small white square as a probe */
    for (int j=0; j<50; j++) for (int i=0; i<50; i++) fb[j*fb_stride + i] = 0xFFFFFF;
    fb_init_done = 1;
}

int fb_is_init(void) { return fb_init_done; }
void fb_get_res(int *w, int *h) { if (w) *w = fb_w; if (h) *h = fb_h; }

void fb_fill(uint32_t color) {
    if (!fb) return;
    for (int y = 0; y < fb_h; ++y) {
        volatile uint32_t *p = fb + (y * fb_stride);
        int n = fb_w;
        while (n--) *p++ = color;
    }
    virtio_gpu_flush();
}

void fb_set_pixel(int x, int y, uint32_t color) {
    if (!fb) return;
    if ((unsigned int)x < (unsigned int)fb_w && (unsigned int)y < (unsigned int)fb_h) {
        fb[y * fb_stride + x] = color;
    }
}

uint32_t fb_get_pixel(int x, int y) {
    if (!fb) return 0;
    if ((unsigned int)x < (unsigned int)fb_w && (unsigned int)y < (unsigned int)fb_h) {
        return fb[y * fb_stride + x];
    }
    return 0;
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb) return;
    /* Clip to screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        volatile uint32_t *p = fb + ((y + i) * fb_stride) + x;
        int n = w;
        while (n--) *p++ = color;
    }
}

void fb_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness) {
    if (!fb || thickness <= 0) return;
    /* top */
    fb_draw_rect(x, y, w, thickness, color);
    /* bottom */
    fb_draw_rect(x, y + h - thickness, w, thickness, color);
    /* left */
    fb_draw_rect(x, y + thickness, thickness, h - 2 * thickness, color);
    /* right */
    fb_draw_rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void fb_draw_hline(int x1, int x2, int y, uint32_t color) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    for (int x = x1; x <= x2; x++) fb_set_pixel(x, y, color);
}

void fb_draw_vline(int x, int y1, int y2, uint32_t color) {
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++) fb_set_pixel(x, y, color);
}

#define GLYPH_CACHE_SIZE 256
static uint32_t glyph_cache[GLYPH_CACHE_SIZE][5 * 7];
static char glyph_cache_chars[GLYPH_CACHE_SIZE];
static uint32_t glyph_cache_colors[GLYPH_CACHE_SIZE];
static int glyph_cache_next = 0;
#include "irq.h"

void fb_draw_text(int x, int y, const char *s, uint32_t color, int scale) {
    if (!fb || !s) return;
    int cur_x = x;
    int glyph_w = 5;
    int spacing = 1;
    while (*s) {
        char c = *s++;
        
        /* Fast Cache Lookup (IRQ Protected) */
        unsigned long flags = irq_save();
        int cache_idx = -1;
        for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
            if (glyph_cache_chars[i] == (char)c && glyph_cache_colors[i] == color) {
                cache_idx = i;
                break;
            }
        }
        
        if (cache_idx == -1) {
            /* Populate Cache */
            cache_idx = glyph_cache_next;
            glyph_cache_chars[cache_idx] = (char)c;
            glyph_cache_colors[cache_idx] = color;
            const uint8_t *g = get_glyph(c);
            for (int r = 0; r < 7; r++) {
                uint8_t bits = g[r];
                for (int col = 0; col < 5; col++) {
                    int bit = (bits >> (4 - col)) & 1;
                    glyph_cache[cache_idx][r * 5 + col] = bit ? color : 0;
                }
            }
            glyph_cache_next = (glyph_cache_next + 1) % GLYPH_CACHE_SIZE;
        }
        irq_restore(flags);
        
        /* Draw from Cache */
        uint32_t *pixels = glyph_cache[cache_idx];
        for (int r = 0; r < 7; r++) {
            for (int col = 0; col < 5; col++) {
                uint32_t p_color = pixels[r * 5 + col];
                if (p_color != 0) {
                    int sx = cur_x + col * scale;
                    int sy = y + r * scale;
                    fb_draw_rect(sx, sy, scale, scale, p_color);
                }
            }
        }
        
        cur_x += (glyph_w * scale) + (spacing * scale);
    }
}

void fb_draw_scaled_glyph(const uint8_t *g, int x, int y, int scale, uint32_t color) {
    /* glyph is 5 cols (bits 0..4), 7 rows */
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = g[row];
        for (int col = 0; col < 5; ++col) {
            int bit = (bits >> (4 - col)) & 1;
            if (bit) {
                /* draw scaled rectangle */
                int sx = x + col*scale;
                int sy = y + row*scale;
                for (int yy = 0; yy < scale; ++yy) {
                    for (int xx = 0; xx < scale; ++xx) {
                        fb_set_pixel(sx + xx, sy + yy, color);
                    }
                }
            }
        }
    }
}

void fb_put_text_centered(const char *s, uint32_t color) {
    if (!fb) return;
    /* compute text pixel width using scale */
    int len = 0; while (s[len]) len++;
    int glyph_w = 5; int glyph_h = 7; int scale = 8; int spacing = scale; /* pixels between glyphs */
    int total_w = len * (glyph_w * scale) + (len - 1) * spacing;
    int start_x = (fb_w - total_w) / 2;
    int start_y = (fb_h - (glyph_h * scale)) / 2;
    int x = start_x;
    for (int i = 0; i < len; ++i) {
        const uint8_t *g = get_glyph(s[i]);
        fb_draw_scaled_glyph(g, x, start_y, scale, color);
        x += glyph_w * scale + spacing;
    }
    virtio_gpu_flush();
}

/* simple terminal at top-left using same 5x7 glyphs scaled small */
static int term_x = 0, term_y = 0;
static const int term_scale = 2;
static const int term_cols = 70; /* 70 chars * 11px = 770px < 800px */
static const int term_rows = 35; /* 35 lines * 15px = 525px < 600px */

void fb_puts(const char *s) {
    if (!fb) return;
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            term_x = 0; term_y++;
            if (term_y >= term_rows) {
                /* simple scroll: clear and reset */
                fb_fill(0x000000);
                term_y = 0;
            }
            continue;
        }
        if (c < 32) continue;
        int px = term_x * (5 * term_scale + 1);
        int py = term_y * (7 * term_scale + 1);
        /* clear background for this char */
        for (int ty = 0; ty < 7 * term_scale + 1; ++ty) {
            for (int tx = 0; tx < 5 * term_scale + 1; ++tx) {
                fb_set_pixel(px + tx, py + ty, 0x000000);
            }
        }
        const uint8_t *g = get_glyph(c);
        fb_draw_scaled_glyph(g, px, py, term_scale, 0xFFFFFFFF);
        term_x++;
        if (term_x >= term_cols) { term_x = 0; term_y++; }
        if (term_y >= term_rows) { fb_fill(0x000000); term_y = 0; }
    }
    virtio_gpu_flush();
}

void fb_draw_bitmap_scaled(int x, int y, int w, int h, const uint32_t *bitmap, int bw, int bh, int cx, int cy, int cw, int ch) {
    if (!fb || !bitmap) return;
    
    /* Clipping */
    /* Intersection of target rect (x,y,w,h) and clip rect (cx,cy,cw,ch) */
    int ix = (x > cx) ? x : cx;
    int iy = (y > cy) ? y : cy;
    int iw = (x + w < cx + cw) ? (x + w) : (cx + cw);
    int ih = (y + h < cy + ch) ? (y + h) : (cy + ch);
    
    iw -= ix;
    ih -= iy;
    
    if (iw <= 0 || ih <= 0) return;

    /* For nearest neighbor scaling:
       src_x = (dst_x - x) * bw / w
       src_y = (dst_y - y) * bh / h
    */

    for (int dy = 0; dy < ih; dy++) {
        int screen_y = iy + dy;
        /* Screen bounds check */
        if (screen_y < 0 || screen_y >= fb_h) continue;
        
        /* Map screen_y back to source y */
        /* relative y in dst rect */
        int rel_y = screen_y - x; 
        /* Wait, screen_y - y is correct relative y */
        rel_y = screen_y - y;
        
        int src_y = (rel_y * bh) / h;
        if (src_y < 0) src_y = 0;
        if (src_y >= bh) src_y = bh - 1;

        volatile uint32_t *row_dst = fb + (screen_y * fb_stride);
        const uint32_t *row_src_base = bitmap + (src_y * bw);

        for (int dx = 0; dx < iw; dx++) {
            int screen_x = ix + dx;
            if (screen_x < 0 || screen_x >= fb_w) continue;

            int rel_x = screen_x - x;
            int src_x = (rel_x * bw) / w;
            if (src_x < 0) src_x = 0;
            if (src_x >= bw) src_x = bw - 1;

            uint32_t color = row_src_base[src_x];
            
            /* Simple Alpha Blend */
            uint8_t a = (color >> 24) & 0xFF;
            if (a == 0) continue;
            if (a == 255) {
                row_dst[screen_x] = color;
            } else {
                /* read dst */
                uint32_t dst = row_dst[screen_x];
                uint8_t dr = (dst >> 16) & 0xFF;
                uint8_t dg = (dst >> 8) & 0xFF;
                uint8_t db = dst & 0xFF;
                
                uint8_t sr = (color >> 16) & 0xFF;
                uint8_t sg = (color >> 8) & 0xFF;
                uint8_t sb = color & 0xFF;

                uint16_t inv_a = 255 - a;
                uint8_t r = (uint8_t)((sr * a + dr * inv_a) / 255);
                uint8_t g = (uint8_t)((sg * a + dg * inv_a) / 255);
                uint8_t b = (uint8_t)((sb * a + db * inv_a) / 255);
                
                row_dst[screen_x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

