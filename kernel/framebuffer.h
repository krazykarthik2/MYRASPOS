#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

void fb_init(void *addr, int width, int height, int stride_bytes);
void fb_fill(uint32_t color);
void fb_put_text_centered(const char *s, uint32_t color);
void fb_puts(const char *s);
int fb_is_init(void);

#endif
