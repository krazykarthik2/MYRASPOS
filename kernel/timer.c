#include "timer.h"
#include "sched.h"
#include "uart.h"
#include <stdint.h>

/* Software-monotonic fallback timer.
   Using a simple tick counter advanced from the scheduler loop to
   avoid accessing platform MMIO during early init which can cause
   data aborts on some QEMU setups. This provides a reliable
   monotonic clock and sleep behavior for now. */

static volatile uint32_t last_ms = 0;
static uint64_t counter_freq = 0;

void timer_init(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_freq));
    if (counter_freq == 0) counter_freq = 62500000; // Fallback
    
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    last_ms = (uint32_t)(ticks * 1000 / counter_freq);
}

uint32_t timer_get_ms(void) {
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return (uint32_t)(ticks * 1000 / counter_freq);
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t now = timer_get_ms();
    uint32_t wake = now + ms;
    task_block_current_until(wake);
}

void timer_poll_and_advance(void) {
    uint32_t now = timer_get_ms();
    if (now > last_ms) {
        scheduler_tick_advance(now - last_ms);
        last_ms = now;
    }
}
