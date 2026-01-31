#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

void fb_init(void *addr, int width, int height, int stride_bytes);
void fb_set_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);
void fb_draw_hline(int x1, int x2, int y, uint32_t color);
void fb_draw_vline(int x, int y1, int y2, uint32_t color);
void fb_draw_text(int x, int y, const char *s, uint32_t color, int scale);
void fb_puts(const char *s);
int fb_is_init(void);
void fb_fill(uint32_t color);
void fb_put_text_centered(const char *s, uint32_t color);
void fb_get_res(int *w, int *h);
void fb_draw_bitmap_scaled(int x, int y, int w, int h, const uint32_t *bitmap, int bw, int bh, int cx, int cy, int cw, int ch);
void fb_draw_scaled_glyph(const uint8_t *g, int x, int y, int scale, uint32_t color);

#endif
