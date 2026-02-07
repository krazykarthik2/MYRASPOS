/* Minimal virtio helper definitions (skeleton) */
#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include "rpi_fx.h"

#ifndef REAL
int virtio_init(void);
int virtio_gpu_init(void);
void virtio_gpu_flush(void);
int virtio_gpu_get_width(void);
int virtio_gpu_get_height(void);
int virtio_input_init(void);
void virtio_input_poll(void);

int virtio_blk_init(void);
int virtio_blk_rw(uint64_t sector, void *buf, int write);
#endif

#endif
