/*
 * debug_overlay.c  --  On-screen debug panel for real hardware
 *
 * Draws a live status panel in the top-right corner of the framebuffer:
 *
 *   ┌────────────────────────────────────────────────────┐
 *   │  ◉ USB KB   connected   MOD:01  KEY:0x04 → 'a'    │
 *   │  ◉ USB MS   connected   X: 345  Y: 200  BTN:L--   │
 *   │  ◉ EMMC     ok          LBA:0x002A8000             │
 *   │  ◉ DISKFS   ok          files:2 R:14 W:3           │
 *   └────────────────────────────────────────────────────┘
 *
 * Only compiled when -DREAL is set; included via #ifdef in wm.c.
 */

#include "debug_overlay.h"
#include "framebuffer.h"
#include <stdint.h>
#include <string.h>

/* ── State store ────────────────────────────────────────────────────── */
static struct {
    /* keyboard */
    int     kb_connected;
    int     kb_usb_ok;
    uint8_t kb_mods;
    uint8_t kb_last_hid;
    int     kb_last_scan;
    char    kb_last_ascii;

    /* mouse */
    int     ms_connected;
    int     ms_usb_ok;
    int     ms_x;
    int     ms_y;
    uint8_t ms_btns;

    /* EMMC */
    int      emmc_ok;
    uint32_t emmc_lba;

    /* diskfs */
    int     dfs_ok;
    int     dfs_files;

    /* block I/O */
    int dfs_reads;
    int dfs_writes;
    int dfs_last_err;
} g;

/* ── Setters ─────────────────────────────────────────────────────────── */
void dbg_set_kb(int connected, int usb_ok,
                uint8_t mods, uint8_t last_hid,
                int last_scan, char last_ascii) {
    g.kb_connected  = connected;
    g.kb_usb_ok     = usb_ok;
    g.kb_mods       = mods;
    g.kb_last_hid   = last_hid;
    g.kb_last_scan  = last_scan;
    g.kb_last_ascii = last_ascii;
}

void dbg_set_mouse(int connected, int usb_ok,
                   int x, int y, uint8_t btns) {
    g.ms_connected = connected;
    g.ms_usb_ok    = usb_ok;
    g.ms_x = x; g.ms_y = y;
    g.ms_btns = btns;
}

void dbg_set_emmc(int sd_ok, uint32_t part_lba) {
    g.emmc_ok  = sd_ok;
    g.emmc_lba = part_lba;
}

void dbg_set_diskfs(int ok, int num_files) {
    g.dfs_ok    = ok;
    g.dfs_files = num_files;
}

void dbg_inc_read(void)  { g.dfs_reads++;  }
void dbg_inc_write(void) { g.dfs_writes++; }
void dbg_set_last_blk_err(int err) { g.dfs_last_err = err; }

/* ── Tiny integer-to-string helpers (no libc printf) ────────────────── */
static char *u32h(uint32_t v, char *out) {
    /* writes 8 hex digits into out, null-terminates, returns out */
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) { out[i] = hex[v & 0xF]; v >>= 4; }
    out[8] = '\0';
    return out;
}

static char *u32d(uint32_t v, char *out) {
    if (v == 0) { out[0]='0'; out[1]='\0'; return out; }
    char tmp[12]; int i = 0;
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    for (int j = 0; j < i; j++) out[j] = tmp[i-1-j];
    out[i] = '\0';
    return out;
}

static char *i32d(int v, char *out) {
    if (v < 0) { out[0] = '-'; u32d((uint32_t)(-v), out+1); }
    else        { u32d((uint32_t)v, out); }
    return out;
}

/* ── String concat helper ────────────────────────────────────────────── */
static int scat(char *buf, int pos, int max, const char *s) {
    while (*s && pos < max - 1) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

/* ── Draw one panel line ─────────────────────────────────────────────── */
#define PANEL_X_RIGHT_MARGIN  8   /* pixels from right edge */
#define PANEL_Y_TOP           8   /* pixels from top edge   */
#define LINE_H               18   /* pixels per text line   */
#define TEXT_SCALE            1   /* 1 = 8px tall glyphs    */
#define CHAR_W                8   /* pixels per char at scale 1 */
#define PANEL_W             480   /* panel width in pixels  */
#define PANEL_PAD             6   /* inner padding          */
#define NUM_LINES             4

static void draw_line(int panel_x, int line, uint32_t dot_color,
                      const char *label, const char *info)
{
    int y = PANEL_Y_TOP + PANEL_PAD + line * LINE_H;
    int x = panel_x + PANEL_PAD;

    /* Colored dot: green=ok, red=bad, yellow=partial */
    fb_draw_rect(x, y + 3, 8, 8, dot_color);
    x += 12;

    /* Label */
    fb_draw_text(x, y, label, 0xFFCCCCCC, TEXT_SCALE);
    x += (int)(strlen(label) + 1) * CHAR_W;

    /* Info */
    fb_draw_text(x, y, info, 0xFFFFFFFF, TEXT_SCALE);
}

/* ── Main draw entry point ───────────────────────────────────────────── */
void dbg_draw_overlay(int screen_w, int screen_h) {
    if (!fb_is_init()) return;
    (void)screen_h;

    int panel_h = NUM_LINES * LINE_H + PANEL_PAD * 2;
    int panel_x = screen_w - PANEL_W - PANEL_X_RIGHT_MARGIN;

    /* Dark semi-opaque background */
    fb_draw_rect(panel_x, PANEL_Y_TOP, PANEL_W, panel_h, 0xCC111111);
    fb_draw_rect_outline(panel_x, PANEL_Y_TOP, PANEL_W, panel_h, 0xFF333333, 1);

    /* Heading */
    fb_draw_text(panel_x + PANEL_PAD, PANEL_Y_TOP + 2,
                 "── DEBUG OVERLAY ──", 0xFF888888, TEXT_SCALE);

    char buf[80]; int p;
    char tmp[16];

    /* ── Line 0: USB Keyboard ── */
    {
        p = 0;
        if (!g.kb_usb_ok)       p = scat(buf, p, 80, "USB INIT FAIL");
        else if (!g.kb_connected) p = scat(buf, p, 80, "no device");
        else {
            p = scat(buf, p, 80, "MOD:0x");
            p = scat(buf, p, 80, u32h(g.kb_mods, tmp) + 6); /* last 2 hex */
            p = scat(buf, p, 80, " HID:0x");
            p = scat(buf, p, 80, u32h(g.kb_last_hid, tmp) + 6);
            p = scat(buf, p, 80, " SC:");
            p = scat(buf, p, 80, i32d(g.kb_last_scan, tmp));
            if (g.kb_last_ascii >= 0x20 && g.kb_last_ascii < 0x7F) {
                p = scat(buf, p, 80, " '");
                buf[p++] = g.kb_last_ascii;
                buf[p++] = '\''; buf[p] = '\0';
            }
        }
        uint32_t dot = g.kb_usb_ok
                     ? (g.kb_connected ? 0xFF00CC00 : 0xFFFFAA00)
                     : 0xFFCC0000;
        draw_line(panel_x, 1, dot, "KB   ", buf);
    }

    /* ── Line 1: USB Mouse ── */
    {
        p = 0;
        if (!g.ms_usb_ok)       p = scat(buf, p, 80, "USB INIT FAIL");
        else if (!g.ms_connected) p = scat(buf, p, 80, "no device");
        else {
            p = scat(buf, p, 80, "X:");
            p = scat(buf, p, 80, i32d(g.ms_x, tmp));
            p = scat(buf, p, 80, " Y:");
            p = scat(buf, p, 80, i32d(g.ms_y, tmp));
            p = scat(buf, p, 80, " BTN:");
            buf[p++] = (g.ms_btns & 1) ? 'L' : '-';
            buf[p++] = (g.ms_btns & 4) ? 'M' : '-';
            buf[p++] = (g.ms_btns & 2) ? 'R' : '-';
            buf[p]   = '\0';
        }
        uint32_t dot = g.ms_usb_ok
                     ? (g.ms_connected ? 0xFF00CC00 : 0xFFFFAA00)
                     : 0xFFCC0000;
        draw_line(panel_x, 2, dot, "MOUSE", buf);
    }

    /* ── Line 2: EMMC ── */
    {
        extern uint32_t sd_debug_status; // We will add this in rpi_fx.c
        p = 0;
        if (!g.emmc_ok) {
            p = scat(buf, p, 80, "INIT FAILED ST:");
            p = scat(buf, p, 80, u32h(sd_debug_status, tmp));
        } else {
            p = scat(buf, p, 80, "LBA:0x");
            p = scat(buf, p, 80, u32h(g.emmc_lba, tmp));
            p = scat(buf, p, 80, " ST:");
            p = scat(buf, p, 80, u32h(sd_debug_status, tmp));
        }
        uint32_t dot = g.emmc_ok ? 0xFF00CC00 : 0xFFCC0000;
        draw_line(panel_x, 3, dot, "EMMC ", buf);
    }

    /* ── Line 3: diskfs ── */
    {
        p = 0;
        if (!g.dfs_ok) {
            p = scat(buf, p, 80, "INIT FAILED");
        } else {
            p = scat(buf, p, 80, "files:");
            p = scat(buf, p, 80, i32d(g.dfs_files, tmp));
            p = scat(buf, p, 80, " R:");
            p = scat(buf, p, 80, i32d(g.dfs_reads, tmp));
            p = scat(buf, p, 80, " W:");
            p = scat(buf, p, 80, i32d(g.dfs_writes, tmp));
            if (g.dfs_last_err) {
                p = scat(buf, p, 80, " ERR:");
                p = scat(buf, p, 80, i32d(g.dfs_last_err, tmp));
            }
        }
        uint32_t dot = g.dfs_ok ? 0xFF00CC00 : 0xFFCC0000;
        draw_line(panel_x, 4, dot, "DSKFS", buf);
    }
}
