#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* Decode a PNG file from the filesystem into a newly allocated RGBA buffer.
 * Path should be a files-path like "/assets/foo.png".
 * On success, returns 0, sets *w, *h, and *out_buffer (caller must kfree).
 * On error, returns negative and *out_buffer is NULL.
 */
int img_load_png(const char *path, int *w, int *h, uint32_t **out_buffer);

/* Display a PNG file from the filesystem at (x,y) on the framebuffer.
 * Returns 0 on success, negative on error.
 */
int img_display_png(const char *path, int x, int y);

#endif
