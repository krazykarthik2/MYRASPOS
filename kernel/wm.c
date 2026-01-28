#include "wm.h"
#include "framebuffer.h"
#include "uart.h"
#include "kmalloc.h"
#include <string.h>
#include "lib.h"
#include "virtio.h"
#include "pty.h"
#include "sched.h"
#include "input.h"
#include "timer.h"
#include "myra_app.h"
#include "cursor.h"

static struct window *window_list = NULL;
static struct window *focused_window = NULL;
static int next_win_id = 1;
static int desktop_dirty = 1;
static int screen_w = 0, screen_h = 0;
static const int taskbar_h = 32;

static int wm_last_mx = -1, wm_last_my = -1;

static int shift_state = 0;
static int caps_lock = 0;

static uint8_t scan_to_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static uint8_t scan_to_ascii_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

/* Per-window spinlock helpers */
static void wm_lock_window(struct window *win) {
    if (!win) return;
    volatile int *l = &win->input_lock;
    unsigned int tmp;
    __asm__ volatile(
        "1: ldxr %w0, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   stxr %w0, %w2, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   dmb sy\n"
        : "=&r" (tmp)
        : "r" (l), "r" (1)
        : "memory"
    );
}

static void wm_unlock_window(struct window *win) {
    if (!win) return;
    __asm__ volatile("dmb sy" ::: "memory");
    win->input_lock = 0;
}


void wm_init(void) {
    fb_get_res(&screen_w, &screen_h);
    input_init(screen_w, screen_h);
    desktop_dirty = 1; // Mark desktop dirty on init
    task_wake_event(WM_EVENT_ID); // Trigger initial draw
}


/* Global lock for window list protection */
static volatile int wm_global_lock = 0;

static void wm_list_lock(void) {
    if (wm_global_lock == 1) return; // simple re-entry guard? No, wait_for_lock
    unsigned int tmp;
    __asm__ volatile(
        "1: ldxr %w0, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   stxr %w0, %w2, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   dmb sy\n"
        : "=&r" (tmp)
        : "r" (&wm_global_lock), "r" (1)
        : "memory"
    );
}

static void wm_list_unlock(void) {
    __asm__ volatile("dmb sy" ::: "memory");
    wm_global_lock = 0;
}

struct window* wm_create_window(const char *name, int x, int y, int w, int h, void (*render_fn)(struct window*)) {
    struct window *win = kmalloc(sizeof(struct window));
    if (!win) return NULL;

    win->id = next_win_id++;
    strncpy(win->name, name, WM_WINDOW_NAME_MAX - 1);
    win->name[WM_WINDOW_NAME_MAX - 1] = '\0';
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->saved_x = x; win->saved_y = y; win->saved_w = w; win->saved_h = h;
    win->border_color = 0x444444;
    win->title_color = 0x2222FF;
    win->render = render_fn;
    win->on_close = NULL;
    win->user_data = NULL;
    win->input_head = win->input_tail = 0;
    win->input_lock = 0;
    win->tty = NULL;
    win->is_dirty = 1;
    
    wm_list_lock();
    win->next = window_list;
    window_list = win;
    focused_window = win; /* focus new window */
    desktop_dirty = 1;
    wm_list_unlock();
    
    task_wake_event(WM_EVENT_ID);
    return win;
}

void wm_close_window(struct window *win) {
    if (!win) return;
    wm_list_lock();
    struct window **prev = &window_list;
    struct window *curr = window_list;
    while (curr) {
        if (curr == win) {
            *prev = curr->next;
            if (focused_window == win) {
                focused_window = window_list; /* naive: focus next top */
            }
            wm_list_unlock();
            if (win->on_close) {
                win->on_close(win);
            }
            kfree(win);
            desktop_dirty = 1;
            task_wake_event(WM_EVENT_ID);
            return;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    wm_list_unlock();
}

void wm_get_mouse_state(int *x, int *y, int *btn) {
    input_get_mouse_state(x, y, btn);
}


static void wm_bring_to_front(struct window *win) {
    if (!win || window_list == win) return;
    
    struct window **prev = &window_list;
    struct window *curr = window_list;
    while (curr) {
        if (curr == win) {
            *prev = curr->next;
            win->next = window_list;
            window_list = win;
            return;
        }
        prev = &curr->next;
        curr = curr->next;
    }
}

void wm_focus_window(struct window *win) {
    if (!win) return;
    wm_list_lock();
    focused_window = win;
    wm_bring_to_front(win);
    win->is_dirty = 1;
    desktop_dirty = 1;
    wm_list_unlock();
    task_wake_event(WM_EVENT_ID);
}

void wm_request_render(struct window *win) {
    if (!win) return;
    win->is_dirty = 1;
    task_wake_event(WM_EVENT_ID);
}

void wm_draw_rect(struct window *win, int x, int y, int w, int h, uint32_t color) {
    if (!win) return;
    int ox = win->x + 2;
    int oy = (win->state == WM_STATE_FULLSCREEN) ? win->y + 2 : win->y + 22;
    int mw = win->w - 4;
    int mh = (win->state == WM_STATE_FULLSCREEN) ? win->h - 4 : win->h - 24;

    /* Clipping to window content area */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > mw) w = mw - x;
    if (y + h > mh) h = mh - y;
    if (w <= 0 || h <= 0) return;

    fb_draw_rect(ox + x, oy + y, w, h, color);
}

void wm_draw_text(struct window *win, int x, int y, const char *text, uint32_t color, int scale) {
    if (!win) return;
    int ox = win->x + 2;
    int oy = (win->state == WM_STATE_FULLSCREEN) ? win->y + 2 : win->y + 22;
    int mw = win->w - 4;
    int mh = (win->state == WM_STATE_FULLSCREEN) ? win->h - 4 : win->h - 24;

    /* Basic clipping check for start position */
    if (x < 0 || y < 0 || x >= mw || y >= mh) return;

    fb_draw_text(ox + x, oy + y, text, color, scale);
}

int wm_is_focused(struct window *win) {
    if (!win) return 0;
    return focused_window == win;
}

int wm_pop_key_event(struct window *win, struct wm_input_event *ev) {
    if (!win) return 0;
    wm_lock_window(win);
    if (win->input_head == win->input_tail) {
        wm_unlock_window(win);
        return 0;
    }
    *ev = win->input_queue[win->input_tail];
    win->input_tail = (win->input_tail + 1) % WM_INPUT_QUEUE_SIZE;
    wm_unlock_window(win);
    return 1;
}


static void wm_handle_clicks(int is_press) {
    static struct window *drag_win = NULL;
    static int drag_off_x = 0, drag_off_y = 0;

    int mx, my, mbtn;
    input_get_mouse_state(&mx, &my, &mbtn);

    if (!mbtn) {
        drag_win = NULL;
    }
    
    if (mbtn && drag_win) {
        int old_x = drag_win->x;
        int old_y = drag_win->y;
        drag_win->x = mx - drag_off_x;
        drag_win->y = my - drag_off_y;
        if (drag_win->x != old_x || drag_win->y != old_y) {
            drag_win->is_dirty = 1;
            task_wake_event(WM_EVENT_ID);
        }
    }

    if (!is_press) return;

    /* Lock the list while we iterate to handle clicks */
    wm_list_lock();

    // 1. Check Taskbar first (top-most layer)
    if (my >= screen_h - taskbar_h) {
        // MYRA Button: 5...65
        if (mx >= 5 && mx <= 65) {
            myra_app_toggle();
            return;
        }
        // Taskbar entries
        int tx = 75;
        struct window *tw = window_list;
        while (tw) {
            if (mx >= tx && mx <= tx + 80) {
                if (tw->state == WM_STATE_MINIMIZED) wm_set_state(tw, WM_STATE_NORMAL);
                else {
                    focused_window = tw;
                    wm_bring_to_front(tw);
                    desktop_dirty = 1;
                    task_wake_event(WM_EVENT_ID);
                }
                return;
            }
            tx += 85;
            tw = tw->next;
        }
        return; // Clicked taskbar but nothing specific
    }

    // 2. Check Windows (Front to Back)
    struct window *w = window_list;
    while (w) {
        if (w->state != WM_STATE_MINIMIZED) {
            // Check if click is within window bounds
            if (mx >= w->x && mx <= w->x + w->w &&
                my >= w->y && my <= w->y + w->h) {
                
                // Focus and bring to front
                if (focused_window != w) {
                    focused_window = w;
                    desktop_dirty = 1;
                }
                wm_bring_to_front(w);
                task_wake_event(WM_EVENT_ID);

                // Check for buttons in title bar (only if not fullscreen)
                if (w->state != WM_STATE_FULLSCREEN && my < w->y + 22) {
                    // Close button (X)
                    if (mx >= w->x + w->w - 22 && mx <= w->x + w->w - 2) {
                        wm_close_window(w);
                        return;
                    }
                    // Maximize button ([])
                    if (mx >= w->x + w->w - 42 && mx <= w->x + w->w - 24) {
                        if (w->state == WM_STATE_NORMAL) wm_set_state(w, WM_STATE_MAXIMIZED_TASKBAR);
                        else wm_set_state(w, WM_STATE_NORMAL);
                        w->is_dirty = 1;
                        task_wake_event(WM_EVENT_ID);
                        return;
                    }
                    // Minimize button (_)
                    if (mx >= w->x + w->w - 62 && mx <= w->x + w->w - 44) {
                        wm_set_state(w, WM_STATE_MINIMIZED);
                        return;
                    }
                    /* Clicked title bar but not a button -> Start Drag */
                    drag_win = w;
                    drag_off_x = mx - w->x;
                    drag_off_y = my - w->y;
                }
                return; // Handled top-most window
            }
        }
        w = w->next;
    }
    wm_list_unlock();
}

static void draw_taskbar(void) {
    /* Draw taskbar background */
    fb_draw_rect(0, screen_h - taskbar_h, screen_w, taskbar_h, 0x111111);
    fb_draw_hline(0, screen_w - 1, screen_h - taskbar_h, 0x555555);
    
    /* Draw "Start" button placeholder */
    fb_draw_rect(5, screen_h - taskbar_h + 5, 60, taskbar_h - 10, 0x00AA00);
    fb_draw_text(10, screen_h - taskbar_h + 8, "VALLI", 0xFFFFFF, 2);

    /* Draw window entries - CRITICAL: Use locking to prevent race with window creation/deletion */
    int x = 75;
    wm_list_lock();
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
    wm_list_unlock();
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
    desktop_dirty = 1;
    task_wake_event(WM_EVENT_ID);
}

void wm_compose(void) {
    if (!fb_is_init()) return;

    /* 1. ALWAYS pop all mouse events to keep the queue healthy and detect clicks.
     * Note: coordinate updates are now handled in the input driver. */
    struct input_event ev;
    while (input_pop_mouse_event(&ev)) {
        if (ev.type == INPUT_TYPE_MOUSE_BTN) {
            if (ev.code == 0x110 && ev.value) { // Left Button Pressed
                wm_handle_clicks(1);
            }
        }
    }
    
    /* 2. Process keyboard events and distribute to focus */
    struct input_event kev;
    while (input_pop_key_event(&kev)) {
        /* INTERCEPT META KEY (Scan Code 125) */
        if (kev.type == INPUT_TYPE_KEY && kev.code == 125 && kev.value == 1) {
            myra_app_toggle();
            continue; /* swallowed by system */
        }

        if (focused_window) {
            wm_lock_window(focused_window);
            int next_head = (focused_window->input_head + 1) % WM_INPUT_QUEUE_SIZE;
            if (next_head != focused_window->input_tail) {
                focused_window->input_queue[focused_window->input_head].type = kev.type;
                focused_window->input_queue[focused_window->input_head].code = kev.code;
                focused_window->input_queue[focused_window->input_head].value = kev.value;
                focused_window->input_head = next_head;
                focused_window->is_dirty = 1;
                
                /* TTY Streaming: if window has a tty, push ASCII directly */
                if (focused_window->tty && kev.type == INPUT_TYPE_KEY) {
                    if (kev.code == 0x2A || kev.code == 0x36) {
                        shift_state = kev.value;
                    } else if (kev.code == 0x3A && kev.value == 1) { // CapsLock press
                        caps_lock = !caps_lock;
                    } else if (kev.value >= 1) {
                        if (kev.code < sizeof(scan_to_ascii)) {
                            char base = scan_to_ascii[kev.code];
                            char shifted = scan_to_ascii_shift[kev.code];
                            char ch = (shift_state ? shifted : base);
                            if (caps_lock && base >= 'a' && base <= 'z') {
                                ch = (shift_state ? base : shifted);
                            }
                            if (ch != 0) pty_write_in(focused_window->tty, ch);
                        }
                    }
                }
            }
            wm_unlock_window(focused_window);
        }
    }

    /* 3. Handle dragging / non-press UI state */
    wm_handle_clicks(0);

    /* 4. Check if we actually need to redraw. */
    int mx, my, mbtn;
    wm_get_mouse_state(&mx, &my, &mbtn);
    int mouse_moved = (mx != wm_last_mx || my != wm_last_my);

    int any_dirty = desktop_dirty;
    struct window *w_ptr = window_list;
    while (w_ptr) {
        if (w_ptr->is_dirty) { any_dirty = 1; break; }
        w_ptr = w_ptr->next;
    }
    
    if (!any_dirty && !mouse_moved) return;
    
    /* 5. Perform the Draw */
    if (any_dirty) {
        desktop_dirty = 0;
        struct window *w_reset = window_list;
        while (w_reset) {
            w_reset->is_dirty = 0;
            w_reset = w_reset->next;
        }

        /* Desktop background (Steel Blue) */
        fb_draw_rect(0, 0, screen_w, screen_h, 0x4682B4);

    /* Draw windows back to front */
    struct window *stack[16];
    int count = 0;
    wm_list_lock();
    struct window *curr = window_list;
    while (curr && count < 16) {
        stack[count++] = curr;
        curr = curr->next;
    }
    wm_list_unlock();
    
    for (int i = count - 1; i >= 0; i--) {
        struct window *w = stack[i];
        if (w->state != WM_STATE_MINIMIZED) {
            uint32_t border_color = (w == focused_window) ? 0xFFFF00 : 0x444488;
            fb_draw_rect_outline(w->x, w->y, w->w, w->h, border_color, 2);
            if (w->state != WM_STATE_FULLSCREEN) {
                uint32_t title_color = (w == focused_window) ? 0x00AA00 : 0x2222BB;
                fb_draw_rect(w->x + 2, w->y + 2, w->w - 4, 20, title_color);
                fb_draw_text(w->x + 8, w->y + 4, w->name, 0xFFFFFF, 2);
                fb_draw_rect(w->x + w->w - 22, w->y + 2, 20, 20, 0xFF0000);
                fb_draw_text(w->x + w->w - 16, w->y + 4, "X", 0xFFFFFF, 2);
                /* Maximize ([]) */
                fb_draw_rect(w->x + w->w - 42, w->y + 2, 20, 20, 0x00AA00);
                fb_draw_rect_outline(w->x + w->w - 38, w->y + 6, 12, 12, 0xFFFFFF, 1);
                /* Minimize (_) */
                fb_draw_rect(w->x + w->w - 62, w->y + 2, 20, 20, 0xAAAA00);
                fb_draw_hline(w->x + w->w - 58, w->x + w->w - 46, w->y + 16, 0xFFFFFF);
            }
            int content_y = (w->state == WM_STATE_FULLSCREEN) ? w->y + 2 : w->y + 22;
            int content_h = (w->state == WM_STATE_FULLSCREEN) ? w->h - 4 : w->h - 24;
            fb_draw_rect(w->x + 2, content_y, w->w - 4, content_h, 0x000000);
            if (w->render) w->render(w);
        }
    }

    draw_taskbar();
    
    /* After full redraw, we must SAVE the NEW background under the cursor */
    save_bg(mx, my);
    draw_cursor_overlay(mx, my);
    wm_last_mx = mx; wm_last_my = my;
    
    virtio_gpu_flush();
} else if (mouse_moved) {
    /* Only mouse moved - optimized sprite update */
    restore_bg();
    save_bg(mx, my);
    draw_cursor_overlay(mx, my);
    wm_last_mx = mx; wm_last_my = my;
    virtio_gpu_flush();
}
}

static void wm_task(void *arg) {
    (void)arg;
    fb_get_res(&screen_w, &screen_h);
    input_init(screen_w, screen_h);
    
    /* Initial draw */
    desktop_dirty = 1;
    wm_compose();
    
    while (1) {
        task_wait_event(WM_EVENT_ID);
        wm_compose();
    }
}

void wm_start_task(void) {
    cursor_init();
    task_create_with_stack(wm_task, NULL, "wm_compositor", 16);
    task_wake_event(WM_EVENT_ID);
}
