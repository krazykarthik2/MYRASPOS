#include "input.h"
#include "uart.h"
#include <stddef.h>

#define EVENT_QUEUE_SIZE 256

static struct input_event key_queue[EVENT_QUEUE_SIZE];
static int key_head = 0, key_tail = 0;

static struct input_event mouse_queue[EVENT_QUEUE_SIZE];
static int mouse_head = 0, mouse_tail = 0;

/* Simple volatile flag for "lock" (single core, so mostly to prevent interrupt race if they happen) */
static volatile int input_lock = 0;

static void lock(void) {
    unsigned int tmp;
    __asm__ volatile(
        "1: ldxr %w0, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   stxr %w0, %w2, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   dmb sy\n"
        : "=&r" (tmp)
        : "r" (&input_lock), "r" (1)
        : "memory"
    );
}

static void unlock(void) {
    __asm__ volatile("dmb sy" ::: "memory");
    input_lock = 0;
}

void input_push_event(uint16_t type, uint16_t code, int32_t value) {
    lock();
    struct input_event *q;
    int *head, *tail;

    if (type == INPUT_TYPE_KEY) {
        /* Heuristic: Codes >= 0x100 are usually mouse/joystick buttons (e.g. BTN_LEFT=0x110) */
        if (code >= 0x100) {
            q = mouse_queue; head = &mouse_head; tail = &mouse_tail;
            type = INPUT_TYPE_MOUSE_BTN; /* Remap type 1 (KEY) -> 10 (BTN) for consumer */
        } else {
            q = key_queue; head = &key_head; tail = &key_tail;
        }
    } else {
        /* ABS, REL, etc go to mouse */
        q = mouse_queue; head = &mouse_head; tail = &mouse_tail;
    }

    int next = (*head + 1) % EVENT_QUEUE_SIZE;
    if (next != *tail) {
        q[*head].type = type;
        q[*head].code = code;
        q[*head].value = value;
        *head = next;
        
        /* Trace input push */
        /* Log ALL inputs for debugging mouse - DISABLED for performance */
        // if (type == INPUT_TYPE_KEY && value == 1) { 
        //      uart_puts("[input.c] push type="); uart_put_hex(type); 
        //      uart_puts(" code="); uart_put_hex(code); 
        //      uart_puts(" val="); uart_put_hex((uint32_t)value); uart_puts("\n");
        // }
    } else {
        // uart_puts("[input.c] queue full!\n");
    }
    unlock();
}

int input_pop_key_event(struct input_event *ev) {
    lock();
    if (key_tail == key_head) { unlock(); return 0; }
    *ev = key_queue[key_tail];
    key_tail = (key_tail + 1) % EVENT_QUEUE_SIZE;
    unlock();
    return 1;
}

int input_pop_mouse_event(struct input_event *ev) {
    lock();
    if (mouse_tail == mouse_head) { unlock(); return 0; }
    *ev = mouse_queue[mouse_tail];
    mouse_tail = (mouse_tail + 1) % EVENT_QUEUE_SIZE;
    unlock();
    return 1;
}

int input_pop_event(struct input_event *ev) {
    /* Legacy: try keys then mouse */
    if (input_pop_key_event(ev)) return 1;
    return input_pop_mouse_event(ev);
}
