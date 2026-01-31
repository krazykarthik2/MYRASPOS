#include "image_viewer.h"
#include "wm.h"
#include "image.h"
#include "kmalloc.h"
#include "framebuffer.h"
#include "lib.h"
#include "sched.h"
#include "input.h"
#include "virtio.h"
#include <string.h>

struct iv_state {
    struct window *win;
    char path[128];
    uint32_t *img_buf;
    int img_w;
    int img_h;
    int loading_error;
    
    /* Input state for text box */
    int requesting_file;
    char input_buf[128];
    int input_len;
};

static void iv_load_image(struct iv_state *st, const char *path) {
    if (st->img_buf) {
        kfree(st->img_buf);
        st->img_buf = NULL;
    }
    st->loading_error = 0;
    strncpy(st->path, path, 127);
    
    int ret = img_load_png(path, &st->img_w, &st->img_h, &st->img_buf);
    if (ret < 0) {
        st->loading_error = ret;
    } else {
        /* Goal: Window size <= 75% screen size, but fit image. */
        int screen_w, screen_h;
        fb_get_res(&screen_w, &screen_h);
        
        int max_w = (screen_w * 3) / 4;
        int max_h = (screen_h * 3) / 4;
        
        int w = st->img_w;
        int h = st->img_h;
        
        /* Scale down if needed */
        if (w > max_w) {
            h = (h * max_w) / w;
            w = max_w;
        }
        if (h > max_h) {
            w = (w * max_h) / h;
            h = max_h;
        }
        
        /* Ensure min size for UI */
        if (w < 300) w = 300;
        if (h < 200) h = 200;
        
        /* Update window */
        st->win->w = w;
        st->win->h = h;
        st->win->x = (screen_w - w) / 2;
        st->win->y = (screen_h - h) / 2;
        
        /* Ensure we are in normal state when loading new image? */
        if (st->win->state == WM_STATE_FULLSCREEN) {
             wm_set_state(st->win, WM_STATE_NORMAL);
        } else {
             /* Update saved state too if currently normal, so it doesn't restore to old size */
             st->win->saved_w = w; st->win->saved_h = h;
             st->win->saved_x = st->win->x; st->win->saved_y = st->win->y;
        }
    }
}

static void iv_draw(struct window *win) {
    struct iv_state *st = (struct iv_state *)win->user_data;
    if (!st) return;

    /* Fill background */
    wm_draw_rect(win, 0, 0, win->w, win->h, 0x202020);

    if (st->requesting_file) {
        /* Draw File Input UI */
        wm_draw_text(win, 20, 40, "Enter File Path:", 0xFFFFFF, 1);
        wm_draw_rect(win, 20, 60, win->w - 40, 30, 0x444444);
        wm_draw_text(win, 25, 68, st->input_buf, 0xFFFFFF, 1);
        int txt_w = strlen(st->input_buf) * 8; 
        wm_draw_rect(win, 25 + txt_w, 65, 2, 20, 0x00FF00);
        wm_draw_text(win, 20, 100, "[Enter] to Load  [Esc] to Cancel", 0xAAAAAA, 1);
        return;
    }

    if (st->img_buf) {
        /* Calculate Dest Rect to fit image in window maintaining aspect ratio */
        int win_w = win->w; 
        int win_h = win->h;
        /* Content area adjustments are handled by wm_draw_bitmap if we passed simpler coords? 
           No, win->w is full window width. wm_draw_bitmap handles Chrome offsets. 
           We just need to fit into (win->w - 4) x (win->h - 24/4).
        */
        int avail_w = win->w - 4;
        int avail_h = (win->state == WM_STATE_FULLSCREEN) ? win->h - 4 : win->h - 24;
        
        int dst_w = avail_w;
        int dst_h = (avail_w * st->img_h) / st->img_w;
        
        if (dst_h > avail_h) {
            dst_h = avail_h;
            dst_w = (avail_h * st->img_w) / st->img_h;
        }
        
        int dst_x = (avail_w - dst_w) / 2;
        int dst_y = (avail_h - dst_h) / 2;
        
        wm_draw_bitmap(win, dst_x, dst_y, dst_w, dst_h, st->img_buf, st->img_w, st->img_h);
        
        /* Overlay path if not fullscreen/distracting? Maybe just at bottom if space */
        if (win->state != WM_STATE_FULLSCREEN) {
             wm_draw_text(win, 10, avail_h - 10, st->path, 0x00FF00, 1);
        }
    } else {
        if (st->loading_error) {
            wm_draw_text(win, 10, 40, "Error loading image:", 0xFF5555, 1);
            if (st->loading_error == -2) wm_draw_text(win, 10, 60, "File not found", 0xFFFFFF, 1);
            else if (st->loading_error == -7) wm_draw_text(win, 10, 60, "Decode error", 0xFFFFFF, 1);
            else wm_draw_text(win, 10, 60, "Unknown error", 0xFFFFFF, 1);
        } else {
            wm_draw_text(win, 10, 40, "No image loaded.", 0xAAAAAA, 1);
        }
    }
}

static void iv_on_close(struct window *win) {
    struct iv_state *st = (struct iv_state *)win->user_data;
    if (st) {
        if (st->img_buf) kfree(st->img_buf);
        kfree(st);
    }
}

static void iv_task(void *arg) {
    struct iv_state *st = (struct iv_state *)arg;
    
    while (1) {
        if (!st->win) break;
        
        struct wm_input_event ev;
        if (wm_pop_key_event(st->win, &ev)) {
            if (ev.type == INPUT_TYPE_KEY && ev.value == 1) { /* Press */
                if (st->requesting_file) {
                    if (ev.code == 0x1C || ev.code == 0x9C) { /* Enter */
                         if (st->input_len > 0) {
                             st->requesting_file = 0;
                             iv_load_image(st, st->input_buf);
                             wm_request_render(st->win);
                         }
                    } else if (ev.code == 0x01) { /* Esc */
                        wm_close_window(st->win);
                        break;
                    } else if (ev.code == 0x0E) { /* Backspace */
                        if (st->input_len > 0) st->input_buf[--st->input_len] = '\0';
                        wm_request_render(st->win);
                    } else {
                        /* Simple ASCII mapping - reusing known map */
                        static const char keymap[] = "  1234567890-=  qwertyuiop[]\n asdfghjkl;'` \\zxcvbnm,./";
                        if (ev.code < sizeof(keymap)) {
                            char c = keymap[ev.code];
                             if (c > 32 && st->input_len < 127) {
                                 st->input_buf[st->input_len++] = c;
                                 st->input_buf[st->input_len] = '\0';
                                 wm_request_render(st->win);
                             }
                        }
                    }
                } else {
                   /* Normal mode input */
                   if (ev.code == 0x01) { /* Esc */
                       wm_close_window(st->win);
                       break;
                   } else if (ev.code == 33) { /* 'f' key (approx, scan code 33 is 'f') check keymap above: 33 is 'f' */
                       if (st->win->state == WM_STATE_FULLSCREEN) {
                           wm_set_state(st->win, WM_STATE_NORMAL);
                       } else {
                           wm_set_state(st->win, WM_STATE_FULLSCREEN);
                       }
                   }
                }
            }
        }
        
        yield();
    }
    task_set_fn_null(task_current_id());
}

/* Helper to map scan codes to chars (duplicated from myra_app for robustness) */
/* Ideally this goes to input.c or keyboard lib */


void image_viewer_start(const char *path) {
    struct iv_state *st = kmalloc(sizeof(struct iv_state));
    memset(st, 0, sizeof(*st));
    
    st->win = wm_create_window("Image Viewer", 100, 100, 600, 400, iv_draw);
    st->win->user_data = st;
    st->win->on_close = iv_on_close;
    
    if (path) {
        iv_load_image(st, path);
    } else {
        st->requesting_file = 1;
        st->input_len = 0;
        st->input_buf[0] = '\0';
    }
    
    task_create(iv_task, st, "image_viewer");
}
