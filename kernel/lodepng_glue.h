#ifndef LODEPNG_GLUE_H
#define LODEPNG_GLUE_H

#include <stddef.h>

void* lodepng_malloc(size_t size);
void* lodepng_realloc(void* ptr, size_t new_size);
void lodepng_free(void* ptr);

#endif
