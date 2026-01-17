#ifndef WM_H
#define WM_H

#include <stdint.h>

#define WM_WINDOW_NAME_MAX 32

typedef enum {
    WM_STATE_NORMAL,
    WM_STATE_MINIMIZED,
    WM_STATE_MAXIMIZED,
    WM_STATE_FULLSCREEN,
    WM_STATE_MAXIMIZED_TASKBAR
} wm_state_t;

struct window {
    int id;
    char name[WM_WINDOW_NAME_MAX];
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    wm_state_t state;
    uint32_t border_color;
    uint32_t title_color;
    void (*draw_content)(struct window *win);
    struct window *next;
};

void wm_init(void);
struct window* wm_create_window(const char *name, int x, int y, int w, int h, void (*draw_fn)(struct window*));
void wm_update(void);
void wm_set_state(struct window *win, wm_state_t state);
void wm_compose(void);
void wm_start_task(void);

#endif
