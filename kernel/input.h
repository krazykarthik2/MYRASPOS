#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define INPUT_TYPE_SYN      0
#define INPUT_TYPE_KEY      1
#define INPUT_TYPE_REL      2
#define INPUT_TYPE_ABS      3
#define INPUT_TYPE_MSC      4

/* Internal normalized types */
#define INPUT_TYPE_MOUSE_BTN 10

struct input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

void input_push_event(uint16_t type, uint16_t code, int32_t value);
int input_pop_event(struct input_event *ev);
int input_pop_key_event(struct input_event *ev);
int input_pop_mouse_event(struct input_event *ev);

#endif
