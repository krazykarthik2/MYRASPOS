#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define INPUT_TYPE_KEY      1
#define INPUT_TYPE_MOUSE_ABS 2
#define INPUT_TYPE_MOUSE_BTN 3

struct input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

void input_push_event(uint16_t type, uint16_t code, int32_t value);
int input_pop_event(struct input_event *ev);

#endif
