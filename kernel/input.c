#include "input.h"
#include "uart.h"
#include <stddef.h>
#include "irq.h"
#include "sched.h"

#define EVENT_QUEUE_SIZE 256

static struct input_event key_queue[EVENT_QUEUE_SIZE];
static int key_head = 0, key_tail = 0;

static struct input_event mouse_queue[EVENT_QUEUE_SIZE];
static int mouse_head = 0, mouse_tail = 0;

/* Simple volatile flag for "lock" (single core, so mostly to prevent interrupt race if they happen) */
static volatile int input_lock = 0;

static unsigned long lock(void) {
    unsigned long flags = irq_save();
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
    return flags;
}

static void unlock(unsigned long flags) {
    __asm__ volatile("dmb sy" ::: "memory");
    input_lock = 0;
    irq_restore(flags);
}

/* Normalized mouse state */
static int mouse_x = 0, mouse_y = 0, mouse_btn = 0;
static int screen_w = 800, screen_h = 600;

void input_init(int sw, int sh) {
    screen_w = sw;
    screen_h = sh;
    mouse_x = sw / 2;
    mouse_y = sh / 2;
}

void input_get_mouse_state(int *x, int *y, int *btn) {
    unsigned long flags = irq_save();
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (btn) *btn = mouse_btn;
    irq_restore(flags);
}

static int scale_mouse(int val, int max_res) {
    /* Virtio ABS is 0..32767 */
    return (val * max_res) / 32768;
}

void input_push_event(uint16_t type, uint16_t code, int32_t value) {
    /* Update global state immediately for low-latency cursor */
    if (type == INPUT_TYPE_ABS) {
        if (code == 0) mouse_x = scale_mouse(value, screen_w);
        else if (code == 1) mouse_y = scale_mouse(value, screen_h);
    } else if (type == INPUT_TYPE_REL) {
        if (code == 0) mouse_x += value;
        else if (code == 1) mouse_y += value;
    } else if (type == INPUT_TYPE_KEY && code >= 0x110) {
        /* Mouse buttons are keys above 0x110 */
        if (code == 0x110) mouse_btn = value;
    }

    /* Clamp coordinates */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= screen_w) mouse_x = screen_w - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= screen_h) mouse_y = screen_h - 1;

    unsigned long flags = lock();
    struct input_event *q;
    int *head, *tail;

    if (type == INPUT_TYPE_KEY) {
        if (code >= 0x100) {
            q = mouse_queue; head = &mouse_head; tail = &mouse_tail;
            type = INPUT_TYPE_MOUSE_BTN;
        } else {
            q = key_queue; head = &key_head; tail = &key_tail;
        }
    } else {
        q = mouse_queue; head = &mouse_head; tail = &mouse_tail;
    }

    int pushed = 0;
    int next = (*head + 1) % EVENT_QUEUE_SIZE;
    if (next != *tail) {
        q[*head].type = type;
        q[*head].code = code;
        q[*head].value = value;
        *head = next;
        pushed = 1;
    }
    unlock(flags);

    if (!pushed) return;

    if (type == INPUT_TYPE_KEY) {
        task_wake_event(WM_EVENT_ID);
    } else {
        task_wake_event(MOUSE_EVENT_ID);
        task_wake_event(WM_EVENT_ID);
    }
}

int input_pop_key_event(struct input_event *ev) {
    unsigned long flags = lock();
    if (key_tail == key_head) { unlock(flags); return 0; }
    *ev = key_queue[key_tail];
    key_tail = (key_tail + 1) % EVENT_QUEUE_SIZE;
    unlock(flags);
    return 1;
}

int input_pop_mouse_event(struct input_event *ev) {
    unsigned long flags = lock();
    if (mouse_tail == mouse_head) { unlock(flags); return 0; }
    *ev = mouse_queue[mouse_tail];
    mouse_tail = (mouse_tail + 1) % EVENT_QUEUE_SIZE;
    unlock(flags);
    return 1;
}

int input_pop_event(struct input_event *ev) {
    /* Legacy: try keys then mouse */
    if (input_pop_key_event(ev)) return 1;
    return input_pop_mouse_event(ev);
}
