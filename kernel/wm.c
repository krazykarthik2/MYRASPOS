#include "wm.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include <string.h>
#include "lib.h"
#include "virtio.h"
#include "sched.h"
#include "input.h"
#include "timer.h"

static struct window *window_list = NULL;
static int next_win_id = 1;
static int screen_w = 0, screen_h = 0;
static const int taskbar_h = 32;

static int mouse_x = 0, mouse_y = 0;
static int mouse_btn = 0;

/* Scale mouse from 0..32767 to screen res */
static int scale_mouse(int val, int max_res) {
    return (val * max_res) / 32767;
}

void wm_init(void) {
    fb_get_res(&screen_w, &screen_h);
}

struct window* wm_create_window(const char *name, int x, int y, int w, int h, void (*draw_fn)(struct window*)) {
    struct window *win = kmalloc(sizeof(struct window));
    if (!win) return NULL;

    win->id = next_win_id++;
    strncpy(win->name, name, WM_WINDOW_NAME_MAX - 1);
    win->name[WM_WINDOW_NAME_MAX - 1] = '\0';
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->saved_x = x; win->saved_y = y; win->saved_w = w; win->saved_h = h;
    win->state = WM_STATE_NORMAL;
    win->border_color = 0x444444;
    win->title_color = 0x2222FF;
    win->draw_content = draw_fn;
    
    win->next = window_list;
    window_list = win;
    
    return win;
}

void wm_set_state(struct window *win, wm_state_t state) {
    if (!win) return;
    
    /* save current if moving from normal */
    if (win->state == WM_STATE_NORMAL) {
        win->saved_x = win->x; win->saved_y = win->y;
        win->saved_w = win->w; win->saved_h = win->h;
    }

    win->state = state;

    switch (state) {
        case WM_STATE_MAXIMIZED:
            win->x = 0; win->y = 0;
            win->w = screen_w; win->h = screen_h;
            break;
        case WM_STATE_FULLSCREEN:
            win->x = 0; win->y = 0;
            win->w = screen_w; win->h = screen_h;
            break;
        case WM_STATE_MAXIMIZED_TASKBAR:
            win->x = 0; win->y = 0;
            win->w = screen_w; win->h = screen_h - taskbar_h;
            break;
        case WM_STATE_NORMAL:
            win->x = win->saved_x; win->y = win->saved_y;
            win->w = win->saved_w; win->h = win->saved_h;
            break;
        case WM_STATE_MINIMIZED:
            /* handled by compositor skipping it */
            break;
    }
}

static void draw_taskbar(void) {
    /* Draw taskbar background */
    fb_draw_rect(0, screen_h - taskbar_h, screen_w, taskbar_h, 0x111111);
    fb_draw_hline(0, screen_w - 1, screen_h - taskbar_h, 0x555555);
    
    /* Draw "Start" button placeholder */
    fb_draw_rect(5, screen_h - taskbar_h + 5, 60, taskbar_h - 10, 0x00AA00);
    fb_draw_text(10, screen_h - taskbar_h + 8, "MYRA", 0xFFFFFF, 2);

    /* Draw window entries */
    int x = 75;
    struct window *w = window_list;
    while (w) {
        uint32_t color = (w->state == WM_STATE_MINIMIZED) ? 0x333333 : 0x5555FF;
        fb_draw_rect(x, screen_h - taskbar_h + 5, 80, taskbar_h - 10, color);
        
        char shortname[9];
        strncpy(shortname, w->name, 8); shortname[8] = '\0';
        fb_draw_text(x + 5, screen_h - taskbar_h + 10, shortname, 0xFFFFFF, 1);
        
        x += 85;
        w = w->next;
    }
}

static void draw_cursor(void) {
    /* Simple arrow/cross */
    fb_draw_rect(mouse_x, mouse_y, 8, 2, 0xFFFFFF);
    fb_draw_rect(mouse_x, mouse_y, 2, 8, 0xFFFFFF);
}

void wm_compose(void) {
    if (!fb_is_init()) return;

    /* Poll input before composing */
    virtio_input_poll();
    struct input_event ev;
    while (input_pop_event(&ev)) {
        if (ev.type == INPUT_TYPE_MOUSE_ABS) {
            if (ev.code == 0) mouse_x = scale_mouse(ev.value, screen_w);
            if (ev.code == 1) mouse_y = scale_mouse(ev.value, screen_h);
        } else if (ev.type == INPUT_TYPE_MOUSE_BTN) {
            if (ev.code == 0x110) mouse_btn = ev.value;
        }
    }

    /* 1. Desktop background (Steel Blue) */
    fb_draw_rect(0, 0, screen_w, screen_h, 0x4682B4);

    /* 2. Draw windows back to front (naive) */
    struct window *w = window_list;
    /* Reverse list for drawing? For now just draw in order */
    while (w) {
        if (w->state != WM_STATE_MINIMIZED) {
            /* Border */
            fb_draw_rect_outline(w->x, w->y, w->w, w->h, w->border_color, 2);
            /* Title bar (only if not fullscreen) */
            if (w->state != WM_STATE_FULLSCREEN) {
                fb_draw_rect(w->x + 2, w->y + 2, w->w - 4, 20, w->title_color);
                fb_draw_text(w->x + 8, w->y + 4, w->name, 0xFFFFFF, 2);
            }
            /* Background */
            int content_y = (w->state == WM_STATE_FULLSCREEN) ? w->y + 2 : w->y + 22;
            int content_h = (w->state == WM_STATE_FULLSCREEN) ? w->h - 4 : w->h - 24;
            fb_draw_rect(w->x + 2, content_y, w->w - 4, content_h, 0x000000);
            /* Content */
            if (w->draw_content) w->draw_content(w);
        }
        w = w->next;
    }

    /* 3. Taskbar */
    draw_taskbar();

    /* 4. Cursor */
    draw_cursor();

    /* Flush to GPU */
    virtio_gpu_flush();
}

static void wm_task(void *arg) {
    (void)arg;
    while (1) {
        wm_compose();
        /* ~30 FPS */
        timer_sleep_ms(33);
    }
}

void wm_start_task(void) {
    task_create(wm_task, NULL, "wm_compositor");
}
