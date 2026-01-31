#include "lodepng_glue.h"
#include "kmalloc.h"

void* lodepng_malloc(size_t size) {
    return kmalloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size) {
    return krealloc(ptr, new_size);
}

void lodepng_free(void* ptr) {
    kfree(ptr);
}
