#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
uint32_t timer_get_ms(void);
/* sleep current task for ms */
void timer_sleep_ms(uint32_t ms);
/* poll hardware and advance scheduler tick (call from scheduler loop) */
void timer_poll_and_advance(void);

#endif
