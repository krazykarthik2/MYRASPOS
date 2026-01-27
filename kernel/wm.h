#ifndef WM_H
#define WM_H

#include <stdint.h>
struct pty;

#define WM_WINDOW_NAME_MAX 32

typedef enum {
    WM_STATE_NORMAL,
    WM_STATE_MINIMIZED,
    WM_STATE_MAXIMIZED,
    WM_STATE_FULLSCREEN,
    WM_STATE_MAXIMIZED_TASKBAR
} wm_state_t;

#define WM_INPUT_QUEUE_SIZE 128
struct wm_input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct window {
    int id;
    char name[WM_WINDOW_NAME_MAX];
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    wm_state_t state;
    uint32_t border_color;
    uint32_t title_color;
    void (*draw_content)(struct window *win);
    void (*on_close)(struct window *win);
    struct window *next;
    struct pty *tty;
    
    /* Input stream for this window */
    struct wm_input_event input_queue[WM_INPUT_QUEUE_SIZE];
    int input_head, input_tail;
    volatile int input_lock;
    int is_dirty;
};

void wm_init(void);
struct window* wm_create_window(const char *name, int x, int y, int w, int h, void (*draw_fn)(struct window*));
void wm_update(void);
void wm_set_state(struct window *win, wm_state_t state);
void wm_close_window(struct window *win);
void wm_compose(void);
void wm_get_mouse_state(int *x, int *y, int *btn);
int wm_is_focused(struct window *win);
int wm_pop_key_event(struct window *win, struct wm_input_event *ev);
void wm_start_task(void);

#endif
