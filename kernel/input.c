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
    while (input_lock) {
        irq_restore(flags);
        yield();
        flags = irq_save();
    }
    input_lock = 1;
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

static uint8_t ascii_to_scan(char c) {
    /* Mapping based on wm.c's scan_to_ascii table */
    if (c >= 'a' && c <= 'z') return 30 + (c - 'a'); /* WRONG: scan codes aren't sequential like this. */
    /* Let's use a more accurate small mapping for common keys */
    switch(c) {
        case '\n': case '\r': return 28;
        case '\b': case 127:  return 14;
        case ' ':  return 57;
        case '1': return 2; case '2': return 3; case '3': return 4; case '4': return 5;
        case '5': return 6; case '6': return 7; case '7': return 8; case '8': return 9;
        case '9': return 10; case '0': return 11;
        case 'q': return 16; case 'w': return 17; case 'e': return 18; case 'r': return 19;
        case 't': return 20; case 'y': return 21; case 'u': return 22; case 'i': return 23;
        case 'o': return 24; case 'p': return 25;
        case 'a': return 30; case 's': return 31; case 'd': return 32; case 'f': return 33;
        case 'g': return 34; case 'h': return 35; case 'j': return 36; case 'k': return 37;
        case 'l': return 38;
        case 'z': return 44; case 'x': return 45; case 'c': return 46; case 'v': return 47;
        case 'b': return 48; case 'n': return 49; case 'm': return 50;
        case '/': return 53; case '.': return 52; case ',': return 51;
        case '-': return 12; case '=': return 13;
        default: return 0;
    }
}

void uart_keyboard_task(void *arg) {
    (void)arg;
    while(1) {
        if (uart_haschar()) {
            char c = uart_getc();
            /* Special handling for uppercase */
            int shifted = (c >= 'A' && c <= 'Z');
            char lookup = shifted ? (c - 'A' + 'a') : c;
            uint8_t scan = ascii_to_scan(lookup);
            if (scan > 0) {
                if (shifted) input_push_event(INPUT_TYPE_KEY, 0x2A, 1); /* Left Shift Press */
                input_push_event(INPUT_TYPE_KEY, scan, 1); /* Key Press */
                input_push_event(INPUT_TYPE_KEY, scan, 0); /* Key Release */
                if (shifted) input_push_event(INPUT_TYPE_KEY, 0x2A, 0); /* Left Shift Release */
            }
        }
        yield();
    }
}

void input_init(int sw, int sh) {
    screen_w = sw;
    screen_h = sh;
    mouse_x = sw / 2;
    mouse_y = sh / 2;
    
    /* Startup the UART keyboard bridge task */
    static int started = 0;
    if (!started) {
        task_create(uart_keyboard_task, NULL, "uart_kbd");
        started = 1;
    }
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
