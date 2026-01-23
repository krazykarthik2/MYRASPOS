#include "files_app.h"
#include "wm.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "lib.h"
#include "input.h"
#include "sched.h"
#include "timer.h"
#include "init.h"
#include "uart.h"
#include <string.h>
#include "programs.h"
#include "shell.h"

#define MAX_FILES 40
#define MAX_PATH_LEN 256
#define FILE_LIST_BUF_SIZE 2048

struct file_entry {
    char name[64];
    int is_dir;
};

struct clipboard {
    char source_path[MAX_PATH_LEN];
    int is_cut;
    int active;
};

static struct clipboard g_clipboard = {0};

struct files_state {
    struct window *win;
    char current_path[MAX_PATH_LEN];
    struct file_entry *files;     // Dynamic alloc
    int num_files;
    int selected_index;
    int scroll_offset;
    char *list_buffer;            // Dynamic alloc
    uint32_t last_refresh;
    char search_query[64];
    int search_len;
    int cursor_visible;
    uint32_t last_cursor_toggle;
    int shift_state;
    uint32_t last_periodic_refresh;
};

static struct files_state *g_files = NULL;

static void refresh_file_list(void) {
    if (!g_files) return;
    
    // Clear list
    g_files->num_files = 0;
    
    // Add ".." if not root and no search query
    if (strcmp(g_files->current_path, "/") != 0 && g_files->search_len == 0) {
        strcpy(g_files->files[0].name, "..");
        g_files->files[0].is_dir = 1;
        g_files->num_files++;
    }

    // CALL THE SHELL WITH LS COMMAND: Build command string
    char cmd[MAX_PATH_LEN + 8];
    strcpy(cmd, "ls ");
    strcat(cmd, g_files->current_path);
    
    uart_puts("[files] shell_exec: "); uart_puts(cmd); uart_puts("\n");
    int r = shell_exec(cmd, g_files->list_buffer, FILE_LIST_BUF_SIZE);
    uart_puts("[files] shell result: "); uart_put_hex(r); uart_puts("\n");
    
    if (r > 0) {
        uart_puts("[files] output text: \n"); uart_puts(g_files->list_buffer); uart_puts("\n");
    }
    
    if (r <= 0) {
        return;
    }

    size_t p = 0;
    while (p < (size_t)r && g_files->num_files < MAX_FILES) {
        size_t l = 0;
        while (p + l < (size_t)r && g_files->list_buffer[p + l] != '\n') ++l;
        if (l == 0) { p++; continue; }
        
        char name_buf[64];
        size_t copy_len = l < 63 ? l : 63;
        memcpy(name_buf, &g_files->list_buffer[p], copy_len);
        name_buf[copy_len] = '\0';

        // Filter by search query
        if (g_files->search_len > 0) {
            int match = 0;
            for (int i = 0; i <= (int)copy_len - g_files->search_len; i++) {
                int equal = 1;
                for (int j = 0; j < g_files->search_len; j++) {
                    char c1 = name_buf[i+j];
                    char c2 = g_files->search_query[j];
                    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                    if (c1 != c2) { equal = 0; break; }
                }
                if (equal) { match = 1; break; }
            }
            if (!match) {
                p += l + 1;
                continue;
            }
        }

        // Copy valid entry
        if (g_files->num_files < MAX_FILES) {
            // Check for root prefix (some shell versions return /etc/ instead of etc/)
            char *display_name = name_buf;
            if (name_buf[0] == '/' && strlen(name_buf) > 1) display_name++;
            
            strncpy(g_files->files[g_files->num_files].name, display_name, 63);
            g_files->files[g_files->num_files].name[63] = '\0';
            
            // Detect directory via trailing slash
            int final_len = strlen(g_files->files[g_files->num_files].name);
            if (final_len > 0 && g_files->files[g_files->num_files].name[final_len-1] == '/') {
                 g_files->files[g_files->num_files].is_dir = 1;
                 g_files->files[g_files->num_files].name[final_len-1] = '\0';
            } else {
                 g_files->files[g_files->num_files].is_dir = 0;
            }

            g_files->num_files++;
        }
        p += l + 1;
    }
}

static void files_draw(struct window *win) {
    if (!g_files) return;

    int content_y = win->y + 22;
    int content_h = win->h - 22;

    // Background - Dark Blue-Grey
    fb_draw_rect(win->x + 2, content_y, win->w - 4, content_h - 2, 0x1E1E2E);
    
    // Toolbar (Path + Search)
    fb_draw_rect(win->x + 2, content_y, win->w - 4, 38, 0x11111B);
    fb_draw_text(win->x + 10, content_y + 12, g_files->current_path, 0xCDD6F4, 1);
    
    // Search Bar UI - Take inspiration from Myra app (Steel Blue/Dark theme)
    int search_w = 140;
    int search_x = win->x + win->w - search_w - 15;
    fb_draw_rect(search_x, content_y + 8, search_w, 22, 0x313244);
    fb_draw_rect_outline(search_x, content_y + 8, search_w, 22, 0x6C7086, 1);
    
    // Blinking Cursor Logic
    uint32_t now = timer_get_ms();
    if (now - g_files->last_cursor_toggle > 500) {
        g_files->cursor_visible = !g_files->cursor_visible;
        g_files->last_cursor_toggle = now;
    }

    if (g_files->search_len == 0) {
        fb_draw_text(search_x + 8, content_y + 11, "Search...", 0x6C7086, 1);
    } else {
        fb_draw_text(search_x + 8, content_y + 11, g_files->search_query, 0xF5E0DC, 1);
    }
    
    // Draw cursor after text
    if (g_files->cursor_visible && wm_is_focused(win)) {
        int cursor_x_pos = search_x + 8 + (g_files->search_len * 7);
        if (cursor_x_pos < search_x + search_w - 5) {
            fb_draw_rect(cursor_x_pos, content_y + 11, 2, 16, 0xF5E0DC);
        }
    }
    
    // File List
    int list_y = content_y + 45;
    int item_h = 24;
    int footer_h = 25;
    int list_area_h = content_h - 45 - footer_h; 
    int max_visible = list_area_h / item_h;

    if (g_files->num_files == 0) {
        fb_draw_text(win->x + 20, list_y + 10, "No items found in this directory.", 0x585B70, 1);
    }
    
    for (int i = 0; i < max_visible; i++) {
        int idx = g_files->scroll_offset + i;
        if (idx >= g_files->num_files) break;
        
        int y_pos = list_y + i * item_h;
        
        // Soft Highlight if hovering or selected (selecting via mouse)
        if (idx == g_files->selected_index) {
            fb_draw_rect(win->x + 2, y_pos, win->w - 4, item_h, 0x45475A);
        }
        
        uint32_t icon_color = g_files->files[idx].is_dir ? 0xF9E2AF : 0x89DCEB;
        fb_draw_rect(win->x + 10, y_pos + 6, 12, 12, icon_color);
        
        fb_draw_text(win->x + 30, y_pos + 8, g_files->files[idx].name, 0xCDD6F4, 1);
    }

    // Status Board
    fb_draw_rect(win->x + 2, win->y + win->h - footer_h - 2, win->w - 4, footer_h, 0x11111B);
    char stats[128];
    strcpy(stats, "Items: ");
    int n = g_files->num_files;
    char nbuf[16]; int np=0; if(n==0) nbuf[np++]='0'; else { int d[10], di=0; while(n>0){d[di++]=n%10;n/=10;} for(int j=di-1;j>=0;--j) nbuf[np++]='0'+d[j]; } nbuf[np]=0;
    strcat(stats, nbuf);
    
    /* ADD DEBUG INFO TO GUI */
    strcat(stats, " | CWD: "); strcat(stats, g_files->current_path);
    
    fb_draw_text(win->x + 10, win->y + win->h - footer_h + 3, stats, 0x9399B2, 1);
}

static void files_on_close(struct window *win) {
    (void)win;
    if (g_files) {
        g_files = NULL; 
        // Note: The task will free the memory when it exits loop
    }
}

static void change_dir(const char *new_path) {
    if (strcmp(new_path, "..") == 0) {
        // Go up
        char *last_slash = strrchr(g_files->current_path, '/');
        if (last_slash && last_slash != g_files->current_path) {
            *last_slash = '\0';
        } else if (last_slash == g_files->current_path) {
             // Root
             g_files->current_path[1] = '\0';
        }
    } else {
        // Append
        int len = strlen(g_files->current_path);
        if (g_files->current_path[len-1] != '/') strcat(g_files->current_path, "/");
        strcat(g_files->current_path, new_path);
    }
    refresh_file_list();
    g_files->selected_index = 0;
    g_files->scroll_offset = 0;
}

static void do_paste(void) {
    if (!g_clipboard.active) return;
    
    // Dest path
    char dest_path[MAX_PATH_LEN];
    strcpy(dest_path, g_files->current_path);
    if (dest_path[strlen(dest_path)-1] != '/') strcat(dest_path, "/");
    
    char *fname = strrchr(g_clipboard.source_path, '/');
    if (fname) fname++; else fname = g_clipboard.source_path;
    strcat(dest_path, fname);
    
    char cmd[MAX_PATH_LEN * 2 + 10];
    char dummy_out[64];
    
    if (g_clipboard.is_cut) {
        strcpy(cmd, "mv ");
        strcat(cmd, g_clipboard.source_path);
        strcat(cmd, " ");
        strcat(cmd, dest_path);
        shell_exec(cmd, dummy_out, sizeof(dummy_out));
        g_clipboard.active = 0;
    } else {
        strcpy(cmd, "cp ");
        strcat(cmd, g_clipboard.source_path);
        strcat(cmd, " ");
        strcat(cmd, dest_path);
        shell_exec(cmd, dummy_out, sizeof(dummy_out));
    }
    
    refresh_file_list();
}

static void files_task(void *arg) {
    // IMMEDIATE LOG
    uart_puts("[files] LITERALLY FIRST INSTRUCTION\n");
    struct files_state *st = (struct files_state *)arg;
    if (!st) { uart_puts("[files] FATAL: NULL STATE\n"); return; }
    
    uart_puts("[files] refreshing initial...\n");
    refresh_file_list();
    uart_puts("[files] first refresh done. items="); uart_put_hex(st->num_files); uart_puts("\n");
    
    int last_mouse_btn = 0;
    uint32_t last_click_time = 0;
    uint32_t last_heartbeat = 0;

    while (g_files) {
        uint32_t now = timer_get_ms();
        
        if (now - last_heartbeat > 5000) {
            uart_puts("[files] heartbeat...\n");
            last_heartbeat = now;
        }
        
        // 1. Periodic Refresh (Every 3 seconds)
        if (now - st->last_periodic_refresh > 3000) {
            refresh_file_list();
            st->last_periodic_refresh = now;
            // uart_puts("[files] periodic refresh done\n");
        }

        if (wm_is_focused(st->win)) {
            // uart_puts("[files] Window is focused!\n");
            struct wm_input_event ev;
            while (wm_pop_key_event(st->win, &ev)) {
                if (ev.type == INPUT_TYPE_KEY) {
                    uart_puts("[files] KEY EVENT code="); uart_put_hex(ev.code); 
                    uart_puts(" val="); uart_put_hex(ev.value); uart_puts("\n");

                    if (ev.code == 0x2A || ev.code == 0x36) {
                        st->shift_state = ev.value;
                        continue;
                    }

                    if (ev.value == 1) { // Press
                        static uint8_t s2a[] = {
                            0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
                            '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
                            0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
                            'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
                        };
                        static uint8_t s2as[] = {
                            0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
                            '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
                            0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
                            'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
                        };

                        if (ev.code == 0x0E) { /* Backspace */
                            if (st->search_len > 0) {
                                st->search_query[--st->search_len] = '\0';
                                refresh_file_list();
                            }
                        } else if (ev.code == 0x01) { /* ESC */
                             st->search_len = 0;
                             st->search_query[0] = '\0';
                             refresh_file_list();
                        } else if (ev.code < (int)sizeof(s2a)) {
                            char c = st->shift_state ? s2as[ev.code] : s2a[ev.code];
                            if (c >= 32 && c <= 126 && st->search_len < 63) {
                                st->search_query[st->search_len++] = c;
                                st->search_query[st->search_len] = '\0';
                                refresh_file_list();
                            }
                        }
                        
                        st->cursor_visible = 1;
                        st->last_cursor_toggle = now;
                    }
                }
            }

            // Mouse Interaction
            int mx, my, mbtn;
            wm_get_mouse_state(&mx, &my, &mbtn);
            
            int lx = mx - st->win->x;
            int ly = my - st->win->y - 22;

            if (lx >= 0 && lx < st->win->w && ly >= 45 && ly < st->win->h - 50) {
                int list_ly = ly - 45;
                int idx = st->scroll_offset + list_ly / 24;
                if (idx < st->num_files) {
                    st->selected_index = idx;
                    
                    if (mbtn && !last_mouse_btn) {
                        uint32_t click_now = timer_get_ms();
                        if (click_now - last_click_time < 300) {
                             if (st->files[idx].is_dir) {
                                 change_dir(st->files[idx].name);
                                 st->search_len = 0;
                                 st->search_query[0] = '\0';
                                 refresh_file_list();
                                 st->selected_index = 0;
                                 st->scroll_offset = 0;
                             }
                        }
                        last_click_time = click_now;
                    }
                }
            }
            last_mouse_btn = mbtn;
        }
        
        yield();
    }
    
    if (st->files) kfree(st->files);
    if (st->list_buffer) kfree(st->list_buffer);
    kfree(st);
    task_set_fn_null(task_current_id());
}

void files_app_start(void) {
    if (g_files) return;
    
    g_files = kmalloc(sizeof(struct files_state));
    if (!g_files) return;
    memset(g_files, 0, sizeof(*g_files));

    // Alloc buffers
    g_files->files = kmalloc(sizeof(struct file_entry) * MAX_FILES);
    g_files->list_buffer = kmalloc(FILE_LIST_BUF_SIZE);
    
    if (!g_files->files || !g_files->list_buffer) {
        if (g_files->files) kfree(g_files->files);
        if (g_files->list_buffer) kfree(g_files->list_buffer);
        kfree(g_files);
        g_files = NULL;
        return;
    }
    
    strcpy(g_files->current_path, "/");
    g_files->win = wm_create_window("File Explorer", 100, 100, 400, 300, files_draw);
    g_files->win->on_close = files_on_close;
    
    int tid = task_create(files_task, g_files, "files_app");
    task_set_parent(tid, 1); // Survive launcher exit
    uart_puts("[files] start: task created id="); uart_put_hex(tid); uart_puts("\n");
}
