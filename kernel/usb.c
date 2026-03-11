/*
 * usb.c  --  DWC2 USB Host Driver for Myras OS (BCM2837 / Pi Zero 2W)
 *
 * Confirmed-correct initialization values from circle / uspi bare-metal
 * USB stacks that work on BCM2835/2837:
 *
 *  GUSBCFG: set ONLY ForceHstMode (bit 29), NEVER also ForceDevMode (bit 30)
 *  GAHBCFG: bit 0 (GIntMsk) + bit 5 (DMAEnable)
 *  FIFO:    Total FIFO = 4096 words (0x1000).  Layout:
 *             RX  : size=0x306  (start implicit at 0)
 *             NPTX: start=0x306, size=0x100
 *             PTX : start=0x406, size=0x100
 *           (from circle CUSBHCIDevice, tested on RPi)
 *  HPRT:    When writing to clear W1C bits, do NOT clear PrtPwr (bit 12).
 *           Use the safe_hprt_write() helper which preserves PrtPwr.
 */

#include "usb.h"
#include "rpi_fx.h"
#include "uart.h"
#include "lib.h"
#include "input.h"
#include "sched.h"
#include "debug_overlay.h"
#include <string.h>

/* ── Register helpers ──────────────────────────────────────────────── */
#define REG(off) (*(volatile uint32_t *)(USB_BASE + (off)))
static inline void     _wr(uint32_t off, uint32_t v) { REG(off) = v; }
static inline uint32_t _rd(uint32_t off)              { return REG(off); }

/* Host periodic TX FIFO (not in usb.h since it is not a per-channel reg) */
#define DWC2_HPTXFSIZ   0x100

/* ── Channels ──────────────────────────────────────────────────────── */
#define CH_CTRL   0   /* control EP0  */
#define CH_KB     1   /* keyboard interrupt IN */
#define CH_MOUSE  2   /* mouse interrupt IN */

/* ── Controller state ──────────────────────────────────────────────── */
static int usb_initialized  = 0;
static int port_connected   = 0;
static int next_addr        = 1;
static uint16_t usb_mps0   = 8;

/* ── HID device slots ──────────────────────────────────────────────── */
#define MAX_HID 2
struct hid_dev {
    int      active;
    uint8_t  addr;
    uint8_t  ep;
    uint16_t mps;
    uint8_t  is_mouse;
    int      ch;
    uint32_t last_poll;
};
static struct hid_dev hid_devs[MAX_HID];

/* ── Keyboard/mouse state ──────────────────────────────────────────── */
static uint8_t kb_prev_keys[6] = {0};
static uint8_t kb_prev_mods    = 0;
static uint8_t kb_led_state    = 0;
static uint8_t mouse_prev_btns = 0;

/* ── HID USB → PS/2 Set-1 scancode map ──────────────────────────────── */
static const uint8_t hid_scan_map[] = {
    0, 0, 0, 0,  30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
   50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
    4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
   27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
   65, 66, 67, 68, 87, 88, 70, 71,110,102,104,111,107,109,106,108,
  105,103, 98, 69, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 72, 73,
   82, 71, 83,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};
static const uint8_t hid_mod_scans[8] =
    { 0x1D, 0x2A, 0x38, 0xDB, 0x9D, 0x36, 0xB8, 0xDC };

static uint8_t hid_to_scan(uint8_t hid) {
    return (hid < (uint8_t)sizeof(hid_scan_map)) ? hid_scan_map[hid] : 0;
}

/* ── Delay ─────────────────────────────────────────────────────────── */
static void usb_delay(int ms) {
    for (volatile int i = 0; i < ms * 50000; i++) __asm__ volatile("nop");
}

/* ── HPRT safe write ─────────────────────────────────────────────────
 * The HPRT register contains:
 *   - Read-only bits: 0 (PrtConnSts), 4 (PrtOvrCurrAct), 10-11 (PrtLnSts)
 *   - W1C bits: 1 (PrtConnDet), 3 (PrtEnChng), 5 (PrtOvrCurrChng)
 *   - Dangerous: bit 2 (PrtEna) — writing 1 DISABLES the port
 *   - Writable: bit 8 (PrtRst), bit 12 (PrtPwr)
 *
 * Safe pattern: read HPRT, PRESERVE PrtPwr, clear ONLY the W1C bits
 * that are set (which we want to acknowledge), leave bit 2 (PrtEna)=0.
 * ─────────────────────────────────────────────────────────────────── */
static void hprt_clear_ints(void) {
    uint32_t hprt = _rd(DWC2_HPRT);
    /* Bits to preserve: 12 (PrtPwr) + any non-W1C writable bits */
    /* Bits NOT to touch: 0 (RO), 2 (writing 1 disables port), 4, 10-11 (RO) */
    /* W1C bits to clear: 1, 3, 5 (if set) */
    uint32_t w = hprt;
    w &= ~((1u<<2));                   /* never write 1 to PrtEna — disables port */
    /* Write 1 to only the W1C bits that are currently set */
    w &= ~((1u<<1)|(1u<<3)|(1u<<5));  /* first clear them in our write value */
    w |= (hprt & ((1u<<1)|(1u<<3)|(1u<<5))); /* then set only those that were 1 */
    _wr(DWC2_HPRT, w);
}

/* ── Channel interrupt bits ─────────────────────────────────────────── */
#define HCINT_XFRC   (1u<<0)
#define HCINT_HALT   (1u<<1)
#define HCINT_STALL  (1u<<3)
#define HCINT_NAK    (1u<<4)
#define HCINT_TXERR  (1u<<7)
#define HCINT_DTERR  (1u<<10)

/* ── Halt a channel cleanly ─────────────────────────────────────────── */
static void usb_halt_ch(int ch) {
    uint32_t hcchar = _rd(DWC2_HCCHAR(ch));
    if (!(hcchar & (1u<<31))) { _wr(DWC2_HCINT(ch), 0x7FF); return; }
    hcchar |= (1u<<30) | (1u<<31);   /* ChDis | ChEna */
    _wr(DWC2_HCCHAR(ch), hcchar);
    int t = 200000;
    while ((_rd(DWC2_HCCHAR(ch)) & (1u<<31)) && --t > 0);
    _wr(DWC2_HCINT(ch), 0x7FF);
}

/* ── DMA Cache Coherency ────────────────────────────────────────────── */
static void usb_cache_clean(void *buf, uint16_t len) {
    if (!buf || !len) return;
    uintptr_t start = (uintptr_t)buf & ~63ULL;
    uintptr_t end = ((uintptr_t)buf + len + 63) & ~63ULL;
    for (uintptr_t p = start; p < end; p += 64) {
        __asm__ volatile("dc cvac, %0" : : "r" (p) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

static void usb_cache_invalidate(void *buf, uint16_t len) {
    if (!buf || !len) return;
    uintptr_t start = (uintptr_t)buf & ~63ULL;
    uintptr_t end = ((uintptr_t)buf + len + 63) & ~63ULL;
    for (uintptr_t p = start; p < end; p += 64) {
        __asm__ volatile("dc ivac, %0" : : "r" (p) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* ── Low-level single-packet transfer ───────────────────────────────── */
static int usb_xfer(int ch, uint8_t addr, uint8_t ep,
                    uint8_t etype, uint8_t dir,
                    void *buf, uint16_t len,
                    uint16_t mps, uint8_t pid,
                    int allow_nak)
{
    usb_halt_ch(ch);

    /* HCCHAR */
    uint32_t hcchar = 0;
    hcchar |= (uint32_t)(mps  & 0x7FF);
    hcchar |= (uint32_t)(ep   & 0xF)   << 11;
    hcchar |= dir ? (1u<<15) : 0;
    hcchar |= (uint32_t)(addr & 0x7F)  << 22;
    hcchar |= (uint32_t)(etype & 0x3)  << 18;
    /* Low-speed device? HPRT PrtSpd bits [17:16]: 01=FS, 10=LS */
    uint32_t prtspd = (_rd(DWC2_HPRT) >> 17) & 0x3;
    if (prtspd == 2) hcchar |= (1u<<17);  /* LSPDDEV */

    /* HCTSIZ: 1 packet */
    uint32_t hctsiz = (uint32_t)(len & 0x7FFFF)
                    | (1u << 19)
                    | ((uint32_t)(pid & 0x3) << 29);

    if (len && buf) {
        if (dir == 0) {
            usb_cache_clean(buf, len);
        } else {
            usb_cache_clean(buf, len);
            usb_cache_invalidate(buf, len);
        }
        _wr(DWC2_HCDMA(ch), (uint32_t)(uintptr_t)buf);
    }

    _wr(DWC2_HCINT(ch),    0x7FF);
    _wr(DWC2_HCINTMSK(ch), 0x7FF);
    _wr(DWC2_HCTSIZ(ch),   hctsiz);
    _wr(DWC2_HCCHAR(ch),   hcchar | (1u<<31));

    int timeout = 100000;
    while (timeout--) {
        uint32_t st = _rd(DWC2_HCINT(ch));
        if (st & HCINT_XFRC)  { 
            _wr(DWC2_HCINT(ch), st); 
            if (dir == 1 && len && buf) {
                usb_cache_invalidate(buf, len);
            }
            return 0;  
        }
        if (st & HCINT_STALL) { _wr(DWC2_HCINT(ch), st); return -3; }
        if (st & HCINT_NAK) {
            _wr(DWC2_HCINT(ch), st);
            if (allow_nak) return -4;
            /* NAK: re-arm the channel for retry */
            _wr(DWC2_HCINT(ch),  0x7FF);
            _wr(DWC2_HCCHAR(ch), hcchar | (1u<<31));
            continue;
        }
        if (st & (HCINT_TXERR|HCINT_DTERR|HCINT_HALT)) {
            _wr(DWC2_HCINT(ch), st);
            return -2;
        }
    }
    usb_halt_ch(ch);
    return -10;
}

/* ── Control transfer (SETUP + optional DATA + STATUS) ──────────────── */
static int usb_ctrl(uint8_t addr, struct usb_setup_packet *setup, void *data)
{
    static struct usb_setup_packet __attribute__((aligned(4))) s_buf;
    memcpy(&s_buf, setup, sizeof(s_buf));

    int ret = usb_xfer(CH_CTRL, addr, 0, 0, 0, &s_buf, 8,
                       usb_mps0, 3 /*SETUP*/, 0);
    if (ret < 0) return ret;

    if (setup->wLength > 0) {
        uint8_t dir = (setup->bmRequestType & 0x80) ? 1 : 0;
        ret = usb_xfer(CH_CTRL, addr, 0, 0, dir, data, setup->wLength,
                       usb_mps0, 2 /*DATA1*/, 0);
        if (ret < 0) return ret;
    }

    uint8_t sdir = (setup->bmRequestType & 0x80) ? 0 : 1;
    usb_xfer(CH_CTRL, addr, 0, 0, sdir, NULL, 0, usb_mps0, 2, 0);
    return 0;
}

/* ── GET DESCRIPTOR ─────────────────────────────────────────────────── */
static int usb_get_desc(uint8_t addr, uint8_t dtype, uint8_t didx,
                        void *buf, uint16_t len)
{
    struct usb_setup_packet s = {0};
    s.bmRequestType = 0x80;
    s.bRequest      = USB_REQ_GET_DESCRIPTOR;
    s.wValue        = (uint16_t)((dtype << 8) | didx);
    s.wLength       = len;
    return usb_ctrl(addr, &s, buf);
}

/* ── HID class helpers ───────────────────────────────────────────────── */
static void usb_set_protocol_boot(uint8_t addr, uint8_t iface) {
    struct usb_setup_packet s = {0};
    s.bmRequestType = 0x21;
    s.bRequest      = HID_SET_PROTOCOL;
    s.wIndex        = iface;
    usb_ctrl(addr, &s, NULL);
}

static void usb_set_idle(uint8_t addr, uint8_t iface) {
    struct usb_setup_packet s = {0};
    s.bmRequestType = 0x21;
    s.bRequest      = HID_SET_IDLE;
    s.wIndex        = iface;
    usb_ctrl(addr, &s, NULL);
}

/* ── Port reset ─────────────────────────────────────────────────────── */
static void usb_port_reset(void) {
    uint32_t hprt = _rd(DWC2_HPRT);
    /* Build write value: preserve PrtPwr, don't set W1C bits, set PrtRst */
    uint32_t w = hprt & ~((1u<<1)|(1u<<2)|(1u<<3)|(1u<<5)); /* clear W1C + PrtEna */
    w |= (1u<<8);   /* PrtRst=1 */
    _wr(DWC2_HPRT, w);
    usb_delay(60);  /* hold reset ≥50ms */
    w &= ~(1u<<8);  /* PrtRst=0  */
    _wr(DWC2_HPRT, w);
    usb_delay(20);  /* recovery  */
}

/* ── Register one HID slot ──────────────────────────────────────────── */
static void usb_register_hid(uint8_t addr, uint8_t ep, uint16_t mps,
                              uint8_t is_mouse)
{
    for (int i = 0; i < MAX_HID; i++) {
        if (hid_devs[i].active) continue;
        hid_devs[i].active    = 1;
        hid_devs[i].addr      = addr;
        hid_devs[i].ep        = ep;
        hid_devs[i].mps       = mps ? mps : 8;
        hid_devs[i].is_mouse  = is_mouse;
        hid_devs[i].ch        = is_mouse ? CH_MOUSE : CH_KB;
        hid_devs[i].last_poll = 0;
        uart_puts("[usb] registered ");
        uart_puts(is_mouse ? "MOUSE" : "KB");
        uart_puts(" addr="); uart_put_hex(addr);
        uart_puts(" ep=");   uart_put_hex(ep);
        uart_puts(" mps=");  uart_putu(mps);
        uart_puts("\n");
        if (is_mouse) {
            dbg_set_mouse(1, 1, 0, 0, 0);
        } else {
            dbg_set_kb(1, 1, 0, 0, 0, 0);
        }
        return;
    }
}

/* ── Enumerate device at addr=0 ─────────────────────────────────────── */
static void usb_enumerate_one(void)
{
    uart_puts("[usb] enumerating...\n");
    usb_mps0 = 8;

    /* Step 1: GET_DESCRIPTOR(Device) — first 8 bytes to learn MPS0 */
    static struct usb_device_descriptor __attribute__((aligned(4))) dev_desc;
    memset(&dev_desc, 0, sizeof(dev_desc));
    int r = usb_get_desc(0, USB_DESC_DEVICE, 0, &dev_desc, 8);
    if (r < 0) {
        uart_puts("[usb] GET_DESC(dev) failed, retrying...\n");
        usb_delay(50);
        r = usb_get_desc(0, USB_DESC_DEVICE, 0, &dev_desc, 8);
    }
    if (r < 0) { uart_puts("[usb] GET_DESC(dev) failed\n"); return; }
    usb_mps0 = dev_desc.bMaxPacketSize0 ? dev_desc.bMaxPacketSize0 : 8;
    uart_puts("[usb] MPS0="); uart_putu(usb_mps0); uart_puts("\n");

    /* Step 2: SET_ADDRESS */
    uint8_t new_addr = (uint8_t)(next_addr++);
    {
        struct usb_setup_packet s = {0};
        s.bRequest = USB_REQ_SET_ADDRESS;
        s.wValue   = new_addr;
        if (usb_ctrl(0, &s, NULL) < 0) {
            uart_puts("[usb] SET_ADDRESS failed\n"); return;
        }
        usb_delay(10);
    }
    uart_puts("[usb] addr="); uart_put_hex(new_addr); uart_puts("\n");

    /* Step 3: SET_CONFIGURATION 1 */
    {
        struct usb_setup_packet s = {0};
        s.bRequest = USB_REQ_SET_CONFIG;
        s.wValue   = 1;
        usb_ctrl(new_addr, &s, NULL);
    }
    usb_delay(10);

    /* Step 4: Try config descriptor (optional — we fall back if it fails) */
    static uint8_t __attribute__((aligned(4))) cfg_buf[512];
    memset(cfg_buf, 0, sizeof(cfg_buf));
    int cfg_ok = (usb_get_desc(new_addr, USB_DESC_CONFIG, 0,
                               cfg_buf, sizeof(cfg_buf)) == 0);
    uart_puts(cfg_ok ? "[usb] config OK\n" : "[usb] no config, using defaults\n");

    int found_kb = 0, found_ms = 0;

    if (cfg_ok) {
        uint16_t total = (uint16_t)(cfg_buf[2] | ((uint16_t)cfg_buf[3] << 8));
        if (total > sizeof(cfg_buf)) total = sizeof(cfg_buf);
        uint8_t *p = cfg_buf, *end = cfg_buf + total;
        uint8_t cur_iface = 0, cur_sub = 0, cur_prot = 0;

        while (p < end && p[0] >= 2) {
            if (p[1] == USB_DESC_INTERFACE && p[0] >= 9) {
                cur_iface = p[2]; cur_sub = p[6]; cur_prot = p[7];
                if (cur_sub == 1 && (cur_prot == 1 || cur_prot == 2)) {
                    usb_set_protocol_boot(new_addr, cur_iface);
                    usb_set_idle(new_addr, cur_iface);
                }
            }
            if (p[1] == USB_DESC_ENDPOINT && p[0] >= 7 &&
                (p[2] & 0x80) && (p[3] & 0x3) == 3) { /* Interrupt IN */
                uint8_t ep_n = p[2] & 0x7F;
                uint16_t mps = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
                if (cur_sub == 1 && cur_prot == 1 && !found_kb) {
                    usb_register_hid(new_addr, ep_n, mps, 0); found_kb = 1;
                } else if (cur_sub == 1 && cur_prot == 2 && !found_ms) {
                    usb_register_hid(new_addr, ep_n, mps, 1); found_ms = 1;
                }
            }
            p += p[0];
        }
    }

    /* Fallback: boot assumptions */
    if (!found_kb) {
        uart_puts("[usb] fallback: EP1=KB\n");
        usb_set_protocol_boot(new_addr, 0);
        usb_set_idle(new_addr, 0);
        usb_register_hid(new_addr, 1, 8, 0);
    }
    if (!found_ms) {
        uart_puts("[usb] fallback: EP2=MOUSE\n");
        usb_set_protocol_boot(new_addr, 1);
        usb_set_idle(new_addr, 1);
        usb_register_hid(new_addr, 2, 4, 1);
    }
    uart_puts("[usb] enumeration done\n");
}

/* ── Process keyboard boot report ───────────────────────────────────── */
static void usb_process_kb(const uint8_t *raw) {
    uint8_t mods = raw[0];
    const uint8_t *keys = raw + 2;

    for (int i = 0; i < 8; i++) {
        int was = (kb_prev_mods >> i) & 1, now = (mods >> i) & 1;
        if (now != was) input_push_event(INPUT_TYPE_KEY, hid_mod_scans[i], now);
    }
    kb_prev_mods = mods;

    /* Releases */
    for (int i = 0; i < 6; i++) {
        if (!kb_prev_keys[i]) continue;
        int still = 0;
        for (int j = 0; j < 6; j++) if (keys[j] == kb_prev_keys[i]) { still=1; break; }
        if (!still) { uint8_t sc = hid_to_scan(kb_prev_keys[i]); if (sc) input_push_event(INPUT_TYPE_KEY, sc, 0); }
    }

    /* Presses */
    uint8_t last_hid = 0, last_sc = 0; char last_ch = 0;
    for (int i = 0; i < 6; i++) {
        if (!keys[i]) continue;
        int was = 0;
        for (int j = 0; j < 6; j++) if (kb_prev_keys[j] == keys[i]) { was=1; break; }
        if (!was) {
            uint8_t sc = hid_to_scan(keys[i]);
            if (sc) {
                input_push_event(INPUT_TYPE_KEY, sc, 1);
                last_hid = keys[i]; last_sc = sc;
                static const char row1[] = "qwertyuiop";
                static const char row2[] = "asdfghjkl";
                static const char row3[] = "zxcvbnm";
                if      (sc >= 16 && sc <= 25) last_ch = row1[sc-16];
                else if (sc >= 30 && sc <= 38) last_ch = row2[sc-30];
                else if (sc >= 44 && sc <= 50) last_ch = row3[sc-44];
                else if (sc >= 2  && sc <= 11) last_ch = (char)('0'+(sc-2));
            }
        }
    }
    for (int i = 0; i < 6; i++) kb_prev_keys[i] = keys[i];
    dbg_set_kb(1, 1, mods, last_hid, (int)last_sc, last_ch);
}

/* ── Process mouse boot report ──────────────────────────────────────── */
static void usb_process_mouse(const uint8_t *raw) {
    uint8_t btns = raw[0];
    int8_t  dx   = (int8_t)raw[1];
    int8_t  dy   = (int8_t)raw[2];

    if (dx) input_push_event(INPUT_TYPE_REL, 0, (int32_t)dx);
    if (dy) input_push_event(INPUT_TYPE_REL, 1, (int32_t)dy);

    for (int i = 0; i < 3; i++) {
        int was = (mouse_prev_btns >> i) & 1, now = (btns >> i) & 1;
        if (now != was) input_push_event(INPUT_TYPE_KEY, (uint16_t)(0x110+i), now);
    }
    mouse_prev_btns = btns;

    int mx, my, mb;
    input_get_mouse_state(&mx, &my, &mb);
    dbg_set_mouse(1, 1, mx, my, btns);
}

/* ── Disconnect cleanup ─────────────────────────────────────────────── */
static void usb_disconnect_all(void) {
    for (int i = 0; i < MAX_HID; i++) {
        if (hid_devs[i].active) usb_halt_ch(hid_devs[i].ch);
        hid_devs[i].active = 0;
    }
    next_addr = 1; usb_mps0 = 8;
    memset(kb_prev_keys, 0, sizeof(kb_prev_keys));
    kb_prev_mods = 0; mouse_prev_btns = 0;
    dbg_set_kb(0, 1, 0, 0, 0, 0);
    dbg_set_mouse(0, 1, 0, 0, 0);
    uart_puts("[usb] disconnected\n");
}

/* ── Public: init ───────────────────────────────────────────────────── */
int usb_init(void) {
    if (usb_initialized) return 0;

    /* 1. Power on USB domain via VideoCore mailbox */
    mbox[0] = 8*4; mbox[1] = 0;
    mbox[2] = 0x28001; mbox[3] = 8; mbox[4] = 8;
    mbox[5] = 3;        /* device: USB */
    mbox[6] = 3;        /* ON + WAIT  */
    mbox[7] = 0;
    if (!mbox_call(8, (unsigned int *)mbox) || (mbox[6] & 2)) {
        uart_puts("[usb] power-on FAILED\n");
        dbg_set_kb(0, 0, 0, 0, 0, 0);
        dbg_set_mouse(0, 0, 0, 0, 0);
        return -1;
    }
    uart_puts("[usb] USB powered on\n");
    usb_delay(10);

    /* 2. Soft reset */
    _wr(DWC2_GRSTCTL, 1u);
    { int t = 1000000; while ((_rd(DWC2_GRSTCTL) & 1u)       && --t > 0); }
    { int t = 1000000; while (!(_rd(DWC2_GRSTCTL) & (1u<<31)) && --t > 0); }
    usb_delay(10);

    /* 3. Force HOST mode — set ONLY ForceHstMode (bit 29).
     *    NEVER set ForceDevMode (bit 30) at the same time — they are
     *    mutually exclusive. Setting both causes undefined behavior and
     *    the controller never produces port-connection events.         */
    uint32_t usbcfg = _rd(DWC2_GUSBCFG);
    usbcfg &= ~(1u<<30);   /* clear ForceDevMode */
    usbcfg |=  (1u<<29);   /* set   ForceHstMode */
    _wr(DWC2_GUSBCFG, usbcfg);
    usb_delay(50);          /* ≥25 ms for mode switch per spec */

    /* 4. AHB config: global-interrupt-enable + DMA enable */
    _wr(DWC2_GAHBCFG, (1u<<0) | (1u<<5));

    /* 5. Host config */
    _wr(DWC2_HCFG, 1u);

    /* 6. FIFO layout — total FIFO = 4096 words (0x1000).
     *    Values from circle (tested on BCM2835/2837):
     *      RX  FIFO: 0x306 words  (start implicit = 0)
     *      NPTX FIFO: start=0x306, size=0x100
     *      PTX  FIFO: start=0x406, size=0x100
     *
     *    Previous code set both to 0x1000 which caused them to overlap
     *    (total only = 0x1000 words) → transfers corrupted each other. */
    _wr(DWC2_GRXFSIZ,   0x306);
    _wr(DWC2_GNPTXFSIZ, (0x100u << 16) | 0x306u);
    _wr(DWC2_HPTXFSIZ,  (0x100u << 16) | 0x406u);

    /* 7. Flush FIFOs */
    _wr(DWC2_GRSTCTL, (1u<<5));    /* RxFFlsh */
    { int t=100000; while ((_rd(DWC2_GRSTCTL)&(1u<<5)) && --t>0); }
    _wr(DWC2_GRSTCTL, (1u<<4)|(0x10u<<6)); /* TxFFlsh, TxFNum=all */
    { int t=100000; while ((_rd(DWC2_GRSTCTL)&(1u<<4)) && --t>0); }

    /* 8. Enable port interrupt only */
    _wr(DWC2_GINTMSK, (1u<<24));

    /* 9. Power the port (preserve current HPRT, set PrtPwr) */
    {
        uint32_t hprt = _rd(DWC2_HPRT);
        hprt &= ~((1u<<1)|(1u<<2)|(1u<<3)|(1u<<5));  /* don't W1C or disable */
        hprt |= (1u<<12);  /* PrtPwr */
        _wr(DWC2_HPRT, hprt);
    }
    usb_delay(20);

    memset(hid_devs, 0, sizeof(hid_devs));
    usb_initialized = 1;
    dbg_set_kb(0, 1, 0, 0, 0, 0);
    dbg_set_mouse(0, 1, 0, 0, 0);
    uart_puts("[usb] host ready\n");
    return 0;
}

/* ── Public: poll ───────────────────────────────────────────────────── */
void usb_poll(void) {
    if (!usb_initialized) return;

    /* Clear W1C status bits safely (does NOT clear PrtPwr) */
    hprt_clear_ints();

    /* Check current physical connection */
    int connected = (_rd(DWC2_HPRT) & (1u<<0)) ? 1 : 0;

    if (connected && !port_connected) {
        port_connected = 1;
        uart_puts("[usb] device connected!\n");
        usb_delay(200);     /* debounce */
        usb_port_reset();
        usb_enumerate_one();
    } else if (!connected && port_connected) {
        port_connected = 0;
        usb_disconnect_all();
    }

    if (!port_connected) return;

    /* Poll every active HID device */
    uint32_t now = scheduler_get_tick();
    for (int i = 0; i < MAX_HID; i++) {
        struct hid_dev *d = &hid_devs[i];
        if (!d->active) continue;
        if (now - d->last_poll < 8) continue; /* ~8 ms */
        d->last_poll = now;

        if (!d->is_mouse) {
            static uint8_t kb_buf[8] __attribute__((aligned(4)));
            int ret = usb_xfer(d->ch, d->addr, d->ep, 3, 1,
                               kb_buf, 8, d->mps, 2, 1);
            if (ret == 0) usb_process_kb(kb_buf);
        } else {
            static uint8_t ms_buf[4] __attribute__((aligned(4)));
            int ret = usb_xfer(d->ch, d->addr, d->ep, 3, 1,
                               ms_buf, 4, d->mps, 2, 1);
            if (ret == 0) usb_process_mouse(ms_buf);
        }
    }
}

/* ── Public: set keyboard LEDs ──────────────────────────────────────── */
void usb_set_leds(uint8_t leds) {
    for (int i = 0; i < MAX_HID; i++) {
        if (!hid_devs[i].active || hid_devs[i].is_mouse) continue;
        struct usb_setup_packet s = {0};
        s.bmRequestType = 0x21;
        s.bRequest      = HID_SET_REPORT;
        s.wValue        = (0x02u << 8);
        s.wLength       = 1;
        kb_led_state    = leds;
        usb_ctrl(hid_devs[i].addr, &s, &kb_led_state);
    }
}
