#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

#include <stdint.h>

/* ───── State setters (called from usb.c, diskfs.c, rpi_fx.c) ───────── */

/* USB keyboard */
void dbg_set_kb(int connected, int usb_ok,
                uint8_t mods, uint8_t last_hid,
                int last_scan, char last_ascii);

/* USB mouse */
void dbg_set_mouse(int connected, int usb_ok,
                   int x, int y, uint8_t btns);

/* EMMC / diskfs */
void dbg_set_emmc(int sd_ok, uint32_t part_lba);
void dbg_set_diskfs(int ok, int num_files);

/* Block I/O counters */
void dbg_inc_read(void);
void dbg_inc_write(void);
void dbg_set_last_blk_err(int err);

/* ───── Renderer (called from wm_compose inside #ifdef REAL) ─────────── */
void dbg_draw_overlay(int screen_w, int screen_h);

#endif /* DEBUG_OVERLAY_H */
