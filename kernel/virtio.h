/* Minimal virtio helper definitions (skeleton) */
#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stddef.h>
#include "rpi_fx.h"

#ifndef REAL
int virtio_init(void);
int virtio_gpu_init(void);
void virtio_gpu_flush(void);
void virtio_gpu_flush_rect(int x, int y, int w, int h);
int virtio_gpu_get_width(void);
int virtio_gpu_get_height(void);
int virtio_input_init(void);
void virtio_input_poll(void);

int virtio_blk_init(void);
int virtio_blk_rw(uint64_t sector, void *buf, int write);
void virtio_flush_dcache(void *start, size_t size);
void virtio_invalidate_dcache(void *start, size_t size);
#endif

#endif
