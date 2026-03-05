#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>
/* Display a PNG file from the filesystem at (x,y) on the framebuffer.
 * Path should be a files-path like "/assets/foo.png" or "assets/foo.png".
 * Returns 0 on success, negative on error.
 */
int img_display_png(const char *path, int x, int y);
int img_load_png(const char *path, int *w, int *h, uint32_t **out_buf);

#endif
