/* Simple PNG display wrapper using a small embedded PNG decoder (LodePNG).
 * Decodes into 32-bit RGBA and blits (alpha blended) to framebuffer using
 * existing fb_set_pixel API.
 */

#include "image.h"
#include "files.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "lodepng.h"
#include "lodepng_glue.h"
#include <stdint.h>
#include <string.h>
#include "rpi_fx.h"
#include "virtio.h"
static inline uint32_t rgba_to_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* alpha blend src over dst. colors are 0xRRGGBB and alpha [0..255] */
static inline uint32_t blend_pixel(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    if (sa == 255) return rgba_to_u32(sr, sg, sb);
    if (sa == 0) return dst;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;
    uint16_t inv_a = 255 - sa;
    uint8_t r = (uint8_t)((sr * sa + dr * inv_a) / 255);
    uint8_t g = (uint8_t)((sg * sa + dg * inv_a) / 255);
    uint8_t b = (uint8_t)((sb * sa + db * inv_a) / 255);
    return rgba_to_u32(r, g, b);
}

int img_display_png(const char *path, int x_off, int y_off) {
    if (!fb_is_init()) return -1;

    /* stat file size */
    struct file_stat st;
    if (files_stat(path, &st) < 0) return -2;
    if (st.size == 0) return -3;

    void *buf = kmalloc(st.size);
    if (!buf) return -4;

    int fd = files_open(path, O_RDONLY);
    if (fd < 0) { kfree(buf); return -5; }
    int r = files_read(fd, buf, st.size);
    files_close(fd);
    if (r <= 0) { kfree(buf); return -6; }

    unsigned char *image = NULL;
    unsigned w, h;
    unsigned err = lodepng_decode32(&image, &w, &h, (const unsigned char *)buf, (size_t)r);
    kfree(buf);
    if (err) {
        if (image) lodepng_free(image);
        return -7; /* decode error */
    }

    /* Blit to framebuffer with simple alpha compositing */
    for (unsigned yy = 0; yy < h; ++yy) {
        for (unsigned xx = 0; xx < w; ++xx) {
            unsigned idx = (yy * w + xx) * 4;
            uint8_t sr = image[idx + 0];
            uint8_t sg = image[idx + 1];
            uint8_t sb = image[idx + 2];
            uint8_t sa = image[idx + 3];
            int dst_x = x_off + (int)xx;
            int dst_y = y_off + (int)yy;
            if ((unsigned)dst_x >= (unsigned)0 && (unsigned)dst_x < (unsigned)0x7fffffff &&
                (unsigned)dst_y >= (unsigned)0 && (unsigned)dst_y < (unsigned)0x7fffffff) {
                /* clip against framebuffer inside fb_set_pixel (safe) */
                uint32_t dst = fb_get_pixel(dst_x, dst_y);
                uint32_t out = blend_pixel(dst, sr, sg, sb, sa);
                fb_set_pixel(dst_x, dst_y, out);
            }
        }
    }

    lodepng_free(image);
    virtio_gpu_flush();
    return 0;
}

int img_load_png(const char *path, int *w, int *h, uint32_t **out_buffer) {
    if (!out_buffer || !w || !h) return -1;
    *out_buffer = NULL; *w = 0; *h = 0;

    struct file_stat st;
    if (files_stat(path, &st) < 0) return -2;
    if (st.size == 0) return -3;

    void *buf = kmalloc(st.size);
    if (!buf) return -4;

    int fd = files_open(path, O_RDONLY);
    if (fd < 0) { kfree(buf); return -5; }
    int r = files_read(fd, buf, st.size);
    files_close(fd);
    if (r <= 0) { kfree(buf); return -6; }

    unsigned char *image = NULL;
    unsigned width, height;
    unsigned err = lodepng_decode32(&image, &width, &height, (const unsigned char *)buf, (size_t)r);
    kfree(buf);
    
    if (err) {
        if (image) lodepng_free(image);
        return -7;
    }

    size_t img_size = width * height * 4;
    uint32_t *final_buf = kmalloc(img_size);
    if (!final_buf) {
        lodepng_free(image);
        return -8;
    }

    for (unsigned i = 0; i < width * height; i++) {
        uint8_t r = image[i*4 + 0];
        uint8_t g = image[i*4 + 1];
        uint8_t b = image[i*4 + 2];
        uint8_t a = image[i*4 + 3];
        final_buf[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    
    lodepng_free(image);
    *w = (int)width;
    *h = (int)height;
    *out_buffer = final_buf;
    return 0;
}
