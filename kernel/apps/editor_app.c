#include "wm.h"
#include "kmalloc.h"
#include "lib.h"
#include "sched.h"
#include "input.h"
#include "uart.h"
#include "timer.h"
#include "init.h"
#include "editor_app.h"
#include "files.h"

#define EDITOR_MAX_BUF 65536
#define EDITOR_NAME "Editor"

static uint8_t s2a[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND
} editor_mode_t;

struct editor_state {
    struct window *win;
    char *buffer;
    int size;
    int cursor_pos;
    int scroll_top;
    char filename[64];
    editor_mode_t mode;
    char cmd_buf[64];
    int cmd_len;
    int is_dirty;
    int last_blink;
    int cursor_visible;
};

static struct editor_state *g_editor = NULL;

static void editor_on_close(struct window *win) {
    (void)win;
    if (g_editor) {
        if (g_editor->buffer) kfree(g_editor->buffer);
        kfree(g_editor);
        g_editor = NULL;
    }
}

static void load_file(struct editor_state *st, const char *path) {
    strncpy(st->filename, path, 63);
    st->filename[63] = '\0';
    
    int fd = files_open(path, O_RDONLY);
    if (fd >= 0) {
        int r = files_read(fd, st->buffer, EDITOR_MAX_BUF - 1);
        if (r >= 0) {
            st->size = r;
            st->buffer[st->size] = '\0';
        } else {
            st->size = 0;
            st->buffer[0] = '\0';
        }
        files_close(fd);
    } else {
        st->buffer[0] = '\0';
        st->size = 0;
    }
}

static void save_file(struct editor_state *st) {
    if (st->filename[0] == '\0') return;
    int fd = files_open(st->filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        if (files_write(fd, st->buffer, st->size) >= 0) {
            st->is_dirty = 0;
        }
        files_close(fd);
    }
}

static void insert_char(struct editor_state *st, char c) {
    if (st->size >= EDITOR_MAX_BUF - 1) return;
    memmove(st->buffer + st->cursor_pos + 1, st->buffer + st->cursor_pos, st->size - st->cursor_pos);
    st->buffer[st->cursor_pos++] = c;
    st->size++;
    st->buffer[st->size] = '\0';
    st->is_dirty = 1;
}

static void delete_char(struct editor_state *st) {
    if (st->cursor_pos == 0) return;
    memmove(st->buffer + st->cursor_pos - 1, st->buffer + st->cursor_pos, st->size - st->cursor_pos);
    st->cursor_pos--;
    st->size--;
    st->buffer[st->size] = '\0';
    st->is_dirty = 1;
}

static void editor_draw(struct window *win) {
    if (!g_editor) return;
    struct editor_state *st = g_editor;
    wm_draw_rect(win, 0, 0, win->w, win->h, 0x1E1E1E);
    int cur_line = 0, cur_col = 0;
    int cursor_draw_x = 10, cursor_draw_y = 10;
    for (int i = 0; i <= st->size; i++) {
        char c = st->buffer[i];
        if (i == st->cursor_pos) {
            cursor_draw_x = 10 + cur_col * 8;
            cursor_draw_y = 10 + (cur_line - st->scroll_top) * 14;
        }
        if (c == '\0') break;
        if (cur_line >= st->scroll_top && cur_line < st->scroll_top + (win->h - 50) / 14) {
            if (c != '\n' && c >= 32 && c <= 126) {
                char s[2] = {c, 0};
                wm_draw_text(win, 10 + cur_col * 8, 10 + (cur_line - st->scroll_top) * 14, s, 0xCDD6F4, 1);
            }
        }
        if (c == '\n') { cur_line++; cur_col = 0; } else { cur_col++; }
    }
    if (st->cursor_visible && wm_is_focused(win)) {
        wm_draw_rect(win, cursor_draw_x, cursor_draw_y, 8, 14, (st->mode == MODE_INSERT) ? 0x00FF00 : 0xFFFFFF);
    }
    wm_draw_rect(win, 0, win->h - 54, win->w, 30, 0x11111B);
    char status[128];
    const char *mode_str = (st->mode == MODE_NORMAL) ? "NORMAL" : (st->mode == MODE_INSERT ? "INSERT" : "COMMAND");
    strcpy(status, mode_str); strcat(status, " | "); strcat(status, st->filename[0] ? st->filename : "[No Name]");
    if (st->is_dirty) strcat(status, " [+]");
    if (st->mode == MODE_COMMAND) { strcat(status, " | :"); strcat(status, st->cmd_buf); }
    wm_draw_text(win, 10, win->h - 48, status, 0x89B4FA, 1);
}

static void handle_command(struct editor_state *st) {
    if (strcmp(st->cmd_buf, "w") == 0) { save_file(st); st->mode = MODE_NORMAL; }
    else if (strcmp(st->cmd_buf, "q") == 0) { wm_close_window(st->win); return; }
    else if (strcmp(st->cmd_buf, "wq") == 0) { save_file(st); wm_close_window(st->win); return; }
    else { st->mode = MODE_NORMAL; }
    st->cmd_len = 0; st->cmd_buf[0] = '\0';
}

static void editor_task(void *arg) {
    struct editor_state *st = (struct editor_state *)arg;
    uint32_t last_blink = 0;
    while (g_editor == st) {
        uint32_t now = timer_get_ms();
        if (now - last_blink > 500) { st->cursor_visible = !st->cursor_visible; last_blink = now; wm_request_render(st->win); }
        if (wm_is_focused(st->win)) {
            struct wm_input_event ev;
            while (wm_pop_key_event(st->win, &ev)) {
                if (ev.type == 0x01 && ev.value == 1) {
                    if (st->mode == MODE_INSERT) {
                        if (ev.code == 0x01) { st->mode = MODE_NORMAL; }
                        else if (ev.code == 0x0E) { delete_char(st); }
                        else if (ev.code == 0x1C) { insert_char(st, '\n'); }
                        else if (ev.code < sizeof(s2a)) { char c = s2a[ev.code]; if (c != 0) insert_char(st, c); }
                    } else if (st->mode == MODE_NORMAL) {
                        if (ev.code == 0x17) { st->mode = MODE_INSERT; }
                        else if (ev.code == 0x27) { st->mode = MODE_COMMAND; st->cmd_len = 0; st->cmd_buf[0] = 0; }
                        else if (ev.code == 0x23) { if (st->cursor_pos > 0) st->cursor_pos--; }
                        else if (ev.code == 0x26) { if (st->cursor_pos < st->size) st->cursor_pos++; }
                    } else if (st->mode == MODE_COMMAND) {
                        if (ev.code == 0x01) { st->mode = MODE_NORMAL; }
                        else if (ev.code == 0x1C) { handle_command(st); }
                        else if (ev.code < sizeof(s2a) && st->cmd_len < 63) {
                             char c = s2a[ev.code]; if (c >= 32 && c <= 126) { st->cmd_buf[st->cmd_len++] = c; st->cmd_buf[st->cmd_len] = '\0'; }
                        }
                    }
                    wm_request_render(st->win);
                }
            }
        }
        yield();
    }
    task_set_fn_null(task_current_id());
}

void editor_app_start(const char *filename) {
    if (g_editor) return;
    struct editor_state *st = kmalloc(sizeof(struct editor_state)); memset(st, 0, sizeof(*st));
    st->buffer = kmalloc(EDITOR_MAX_BUF); st->mode = MODE_NORMAL;
    if (filename) load_file(st, filename); else strcpy(st->filename, "untitled.txt");
    st->win = wm_create_window(EDITOR_NAME, 100, 100, 640, 400, editor_draw);
    st->win->on_close = editor_on_close; g_editor = st;
    int tid = task_create(editor_task, st, "valli_editor"); task_set_parent(tid, 1);
}
