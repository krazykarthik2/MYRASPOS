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

static struct window *window_list = NULL;
static struct window *focused_window = NULL;
static int next_win_id = 1;
static int screen_w = 0, screen_h = 0;
static const int taskbar_h = 32;

static int mouse_x = 0, mouse_y = 0;
static int mouse_btn = 0;
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

/* Scale mouse from 0..32767 to screen res */
static int scale_mouse(int val, int max_res) {
    return (val * max_res) / 32767;
}

void wm_init(void) {
    fb_get_res(&screen_w, &screen_h);
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
    win->on_close = NULL;
    win->input_head = win->input_tail = 0;
    win->input_lock = 0;
    win->tty = NULL;
    
    win->input_lock = 0;
    win->tty = NULL;
    
    wm_list_lock();
    win->next = window_list;
    window_list = win;
    focused_window = win; /* focus new window */
    wm_list_unlock();
    
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
            kfree(win);
            break;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    wm_list_unlock();
}

void wm_get_mouse_state(int *x, int *y, int *btn) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (btn) *btn = mouse_btn;
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

static void wm_handle_clicks(void) {
    static int last_btn = 0;
    static struct window *drag_win = NULL;
    static int drag_off_x = 0, drag_off_y = 0;

    int pressed = (mouse_btn && !last_btn);
    int released = (!mouse_btn && last_btn);
    
    if (released) {
        drag_win = NULL;
    }
    
    if (mouse_btn && drag_win) {
        drag_win->x = mouse_x - drag_off_x;
        drag_win->y = mouse_y - drag_off_y;
        /* constrain to screen? optional */
    }

    last_btn = mouse_btn;
    if (!pressed) return;

    // 1. Check Taskbar first (top-most layer)
    if (mouse_y >= screen_h - taskbar_h) {
        // MYRA Button: 5...65
        if (mouse_x >= 5 && mouse_x <= 65) {
            myra_app_open();
            return;
        }
        // Taskbar entries
        int tx = 75;
        struct window *tw = window_list;
        while (tw) {
            if (mouse_x >= tx && mouse_x <= tx + 80) {
                if (tw->state == WM_STATE_MINIMIZED) wm_set_state(tw, WM_STATE_NORMAL);
                else {
                    focused_window = tw;
                    wm_bring_to_front(tw);
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
            if (mouse_x >= w->x && mouse_x <= w->x + w->w &&
                mouse_y >= w->y && mouse_y <= w->y + w->h) {
                
                // Focus and bring to front
                focused_window = w;
                wm_bring_to_front(w);

                // Check for buttons in title bar (only if not fullscreen)
                if (w->state != WM_STATE_FULLSCREEN && mouse_y < w->y + 22) {
                    // Close button (X)
                    if (mouse_x >= w->x + w->w - 22 && mouse_x <= w->x + w->w - 2) {
                        wm_close_window(w);
                        return;
                    }
                    // Maximize button ([])
                    if (mouse_x >= w->x + w->w - 42 && mouse_x <= w->x + w->w - 24) {
                        if (w->state == WM_STATE_NORMAL) wm_set_state(w, WM_STATE_MAXIMIZED_TASKBAR);
                        else wm_set_state(w, WM_STATE_NORMAL);
                        return;
                    }
                    // Minimize button (_)
                    if (mouse_x >= w->x + w->w - 62 && mouse_x <= w->x + w->w - 44) {
                        wm_set_state(w, WM_STATE_MINIMIZED);
                        return;
                    }
                    /* Clicked title bar but not a button -> Start Drag */
                    drag_win = w;
                    drag_off_x = mouse_x - w->x;
                    drag_off_y = mouse_y - w->y;
                }
                return; // Handled top-most window
            }
        }
        w = w->next;
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
    /* Better arrow cursor */
    uint32_t c = 0xFFFFFF;     /* white */
    uint32_t o = 0x000000;     /* black outline */

    /* Outline */
    for(int i=0; i<12; i++) fb_set_pixel(mouse_x+0, mouse_y+i, o);
    for(int i=0; i<8; i++)  fb_set_pixel(mouse_x+i, mouse_y+i, o);
    for(int i=0; i<5; i++)  fb_set_pixel(mouse_x+i, mouse_y+8, o);
    fb_set_pixel(mouse_x+5, mouse_y+9, o);
    fb_set_pixel(mouse_x+6, mouse_y+10, o);
    fb_set_pixel(mouse_x+7, mouse_y+11, o);
    fb_set_pixel(mouse_x+1, mouse_y+12, o);

    /* Fill */
    for(int i=1; i<11; i++) fb_set_pixel(mouse_x+1, mouse_y+i, c);
    for(int i=2; i<7; i++)  fb_set_pixel(mouse_x+2, mouse_y+i, c);
    for(int i=3; i<6; i++)  fb_set_pixel(mouse_x+3, mouse_y+i, c);
    fb_set_pixel(mouse_x+4, mouse_y+4, c);
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

void wm_compose(void) {
    if (!fb_is_init()) return;

    /* Poll input before composing */
    virtio_input_poll();
    struct input_event ev;
    while (input_pop_mouse_event(&ev)) {
        /*
        if (ev.type == INPUT_TYPE_REL || ev.type == INPUT_TYPE_ABS || ev.type == INPUT_TYPE_MOUSE_BTN) {
             uart_puts("[wm] mouse type="); uart_put_hex(ev.type); 
             uart_puts(" code="); uart_put_hex(ev.code); 
             uart_puts(" val="); uart_put_hex(ev.value); uart_puts("\n");
        }
        */

        if (ev.type == INPUT_TYPE_ABS) {
            if (ev.code == 0) mouse_x = scale_mouse(ev.value, screen_w);
            if (ev.code == 1) mouse_y = scale_mouse(ev.value, screen_h);
        } else if (ev.type == INPUT_TYPE_REL) {
            /* Handle Relative Motion (Mouse) */
            if (ev.code == 0) mouse_x += ev.value; /* REL_X */
            if (ev.code == 1) mouse_y += ev.value; /* REL_Y */
            /* Clamp to screen */
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= screen_w) mouse_x = screen_w - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= screen_h) mouse_y = screen_h - 1;
        } else if (ev.type == INPUT_TYPE_MOUSE_BTN) {
            if (ev.code == 0x110) mouse_btn = ev.value;
        }
    }
    
    /* Distribute keyboard events */
    struct input_event kev;
    while (input_pop_key_event(&kev)) {
        uart_puts("[wm] popped key event\n");
        if (focused_window) {
            wm_lock_window(focused_window);
            int next_head = (focused_window->input_head + 1) % WM_INPUT_QUEUE_SIZE;
            if (next_head != focused_window->input_tail) {
                focused_window->input_queue[focused_window->input_head].type = kev.type;
                focused_window->input_queue[focused_window->input_head].code = kev.code;
                focused_window->input_queue[focused_window->input_head].value = kev.value;
                focused_window->input_head = next_head;
                
                /* UART Diagnostic: Event distributed */
                if (kev.value >= 1) { // press or repeat
                    uart_puts("[wm] dist code="); uart_put_hex(kev.code);
                    uart_puts(" val="); uart_put_hex(kev.value);
                    uart_puts(" to '"); uart_puts(focused_window->name); uart_puts("'\n");
                }

                /* TTY Streaming: if window has a tty, push ASCII directly */
                if (focused_window->tty && kev.type == INPUT_TYPE_KEY) {
                    if (kev.code == 0x2A || kev.code == 0x36) {
                        shift_state = kev.value;
                    } else if (kev.code == 0x3A && kev.value == 1) { // CapsLock press
                        caps_lock = !caps_lock;
                    } else if (kev.value >= 1) {
                        if (kev.code < sizeof(scan_to_ascii)) {
                            char ch = 0;
                            /* Simple logic: Shift XOR CapsLock for letters */
                            
                            /* Check if letter (q..p, a..l, z..m) - rough ranges or just check result */
                            /* Actually, better to just pick map. 
                               If CapsLock is ON, we want Uppercase for letters, but NOT for numbers.
                               This requires knowing if it IS a letter. 
                               Let's stick to Shift for now, and add CapsLock if simple.
                            */
                            
                            /* Better: Use scan_to_ascii (lower) types. 
                               If CapsLock is ON and ch is a-z, subtract 32. 
                            */
                            char base = scan_to_ascii[kev.code];
                            char shifted = scan_to_ascii_shift[kev.code];
                            
                            if (caps_lock && base >= 'a' && base <= 'z') {
                                /* CapsLock active on letter: Invert Shift effect? 
                                   No, CapsLock forces Upper. Shift+Caps = Lower. */
                                if (shift_state) ch = base; // Shift+Caps = lower
                                else ch = shifted;          // Caps = upper
                            } else {
                                /* Not a letter or no CapsLock */
                                ch = shift_state ? shifted : base;
                            }

                            if (ch != 0) {
                                // uart_puts("[wm] PTY write char: "); uart_put_hex(ch); uart_puts("\n");
                                pty_write_in(focused_window->tty, ch);
                            }
                        }
                    }
                }
            }
            wm_unlock_window(focused_window);
        } else {
            /* No focus - drop or log */
            if (kev.value >= 1) {
                uart_puts("[wm] WARN: key dropped, no focus. code="); uart_put_hex(kev.code); uart_puts("\n");
            }
        }
    }

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_w) mouse_x = screen_w - 1;
    if (mouse_y >= screen_h) mouse_y = screen_h - 1;

    wm_handle_clicks();

    /* 1. Desktop background (Steel Blue) */
    fb_draw_rect(0, 0, screen_w, screen_h, 0x4682B4);

    /* 2. Draw windows back to front (Tail -> Head) */
    struct window *stack[16];
    int count = 0;
    
    wm_list_lock();
    struct window *curr = window_list;
    while (curr && count < 16) {
        stack[count++] = curr;
        curr = curr->next;
    }
    wm_list_unlock();
    
    // Draw from end of stack (bottom) to beginning (top)
    for (int i = count - 1; i >= 0; i--) {
        struct window *w = stack[i];
        if (w->state != WM_STATE_MINIMIZED) {
            /* Border (Yellow if focused) */
            uint32_t border_color = (w == focused_window) ? 0xFFFF00 : 0x444488;
            fb_draw_rect_outline(w->x, w->y, w->w, w->h, border_color, 2);
            /* Title bar (only if not fullscreen) */
            if (w->state != WM_STATE_FULLSCREEN) {
                uint32_t title_color = (w == focused_window) ? 0x00AA00 : 0x2222BB;
                fb_draw_rect(w->x + 2, w->y + 2, w->w - 4, 20, title_color);
                fb_draw_text(w->x + 8, w->y + 4, w->name, 0xFFFFFF, 2);
                
                /* Draw Buttons */
                /* Close (X) */
                fb_draw_rect(w->x + w->w - 22, w->y + 2, 20, 20, 0xFF0000);
                fb_draw_text(w->x + w->w - 16, w->y + 4, "X", 0xFFFFFF, 2);
                /* Maximize ([]) */
                fb_draw_rect(w->x + w->w - 42, w->y + 2, 20, 20, 0x00AA00);
                fb_draw_rect_outline(w->x + w->w - 38, w->y + 6, 12, 12, 0xFFFFFF, 1);
                /* Minimize (_) */
                fb_draw_rect(w->x + w->w - 62, w->y + 2, 20, 20, 0xAAAA00);
                fb_draw_hline(w->x + w->w - 58, w->x + w->w - 46, w->y + 16, 0xFFFFFF);
            }
            /* Background */
            int content_y = (w->state == WM_STATE_FULLSCREEN) ? w->y + 2 : w->y + 22;
            int content_h = (w->state == WM_STATE_FULLSCREEN) ? w->h - 4 : w->h - 24;
            fb_draw_rect(w->x + 2, content_y, w->w - 4, content_h, 0x000000);
            /* Content */
            if (w->draw_content) w->draw_content(w);
        }
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
        
        /* Heartbeat every ~30 frames (approx 1 sec) */
        static int frames = 0;
        if (++frames % 30 == 0) {
            uart_puts(".");
        }
    }
}

void wm_start_task(void) {
    task_create(wm_task, NULL, "wm_compositor");
}
