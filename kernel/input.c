#include "input.h"
#include <stddef.h>

#define EVENT_QUEUE_SIZE 256

static struct input_event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;

void input_push_event(uint16_t type, uint16_t code, int32_t value) {
    int next = (queue_head + 1) % EVENT_QUEUE_SIZE;
    if (next == queue_tail) return; /* queue full */
    
    event_queue[queue_head].type = type;
    event_queue[queue_head].code = code;
    event_queue[queue_head].value = value;
    queue_head = next;
}

int input_pop_event(struct input_event *ev) {
    if (queue_tail == queue_head) return 0;
    
    *ev = event_queue[queue_tail];
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}
