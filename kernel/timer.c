#include "timer.h"
#include "sched.h"
#include "uart.h"
#include <stdint.h>

/* BCM system timer base used by this platform (adjusted for QEMU mapping used in repo) */
#define SYS_TIMER_BASE 0x09003000
#define SYS_TIMER_CLO  (SYS_TIMER_BASE + 0x4)

static uint32_t last_ms = 0;

static inline uint32_t mmio_read(uint32_t reg) {
    return *(volatile uint32_t *)reg;
}

void timer_init(void) {
    /* initialize monotonic based on system timer CLO (microseconds) */
    uint32_t clo = mmio_read(SYS_TIMER_CLO);
    last_ms = clo / 1000;
}

uint32_t timer_get_ms(void) {
    uint32_t clo = mmio_read(SYS_TIMER_CLO);
    return clo / 1000;
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t now = timer_get_ms();
    uint32_t wake = now + ms;
    /* block current task until wake (scheduler will wake it when tick advances) */
    task_block_current_until(wake);
}

/* Called from scheduler loop occasionally to advance tick based on HW timer */
void timer_poll_and_advance(void) {
    uint32_t now = timer_get_ms();
    if (now != last_ms) {
        uint32_t delta = now - last_ms;
        last_ms = now;
        scheduler_tick_advance(delta);
    }
}
