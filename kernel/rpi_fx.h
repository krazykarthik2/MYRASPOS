#ifndef RPI_FX_H
#define RPI_FX_H

#include <stdint.h>

/*
 * Raspberry Pi Bare Metal Implementations (Stubs)
 * These functions will be called instead of virtio_* when -DREAL is used.
 */

int rpi_init(void);
int rpi_gpu_init(void);
void rpi_gpu_flush(void);
int rpi_gpu_get_width(void);
int rpi_gpu_get_height(void);
int rpi_input_init(void);
void rpi_input_poll(void);

int rpi_blk_init(void);
int rpi_blk_rw(uint64_t sector, void *buf, int write);

void rpi_built_in_led_on(void);
void rpi_built_in_led_off(void);

#ifdef REAL
#define virtio_init rpi_init
#define virtio_gpu_init rpi_gpu_init
#define virtio_gpu_flush rpi_gpu_flush
#define virtio_gpu_get_width rpi_gpu_get_width
#define virtio_gpu_get_height rpi_gpu_get_height
#define virtio_input_init rpi_input_init
#define virtio_input_poll rpi_input_poll
#define virtio_blk_init rpi_blk_init
#define virtio_blk_rw rpi_blk_rw
#endif

#endif
