#include "usb.h"
#include "rpi_fx.h"
#include "uart.h"
#include "lib.h"
#include "input.h"
#include "sched.h"

#define REG(off) (*(volatile uint32_t *)(USB_BASE + (off)))

static void usb_write_reg(uint32_t off, uint32_t val) { REG(off) = val; }
static uint32_t usb_read_reg(uint32_t off) { return REG(off); }

/* Controller State */
static int usb_initialized = 0;
static int usb_device_connected = 0;
static int usb_device_enumerated = 0;
static uint8_t usb_dev_addr = 0;
static uint16_t usb_max_pkt0 = 8;

/* Keyboard Specific */
static int usb_has_keyboard = 0;
static uint8_t usb_kb_ep_in = 0;
static uint32_t usb_last_poll_tick = 0;
static uint8_t usb_current_leds = 0;

/* Channel Management (Simplistic) */
#define CH_CONTROL 0
#define CH_INTR_IN 1



/* Helpers */
static void usb_delay(int ticks) {
    for (volatile int i = 0; i < ticks * 1000; i++) asm volatile("nop");
}

/* Base Transfer Logic */
static int usb_wait_ch(int ch) {
    int timeout = 1000000;
    while (timeout--) {
        uint32_t hcint = usb_read_reg(DWC2_HCINT(ch));
        if (hcint & (1 << 0)) return 0;         // Transfer Completed
        if (hcint & (1 << 1)) return -1;       // CH Halted
        if (hcint & (1 << 3)) return -2;       // STALL
        if (hcint & (1 << 4)) return -3;       // NAK
        if (hcint & (1 << 5)) return -4;       // ACK
        if (hcint & (1 << 7)) return -5;       // Data Error
    }
    return -10; // Timeout
}

static int usb_transfer(int ch, uint8_t dev_addr, uint8_t ep_num, uint8_t ep_type, uint8_t ep_dir, void *buffer, uint32_t len, uint8_t pid) {
    /* 1. Setup HCCHAR */
    uint32_t hcchar = (dev_addr << 22) | (ep_num << 11) | (len > 0 ? (usb_max_pkt0 << 0) : 8);
    hcchar |= (ep_dir ? (1 << 15) : 0); // Direction: 1=IN, 0=OUT
    hcchar |= (ep_type << 18);          // 0=Control, 3=Interrupt
    hcchar |= (usb_read_reg(DWC2_HPRT) & (1 << 17)) ? (1 << 17) : 0; // LS device if port is LS

    /* 2. Setup HCTSIZ */
    uint32_t hctsiz = (len & 0x7FFFF) | (1 << 19) | (pid << 29); // 1 packet
    
    /* 3. DMA Address */
    if (len > 0) {
        usb_write_reg(DWC2_HCDMA(ch), (uint32_t)(uintptr_t)buffer);
    }

    usb_write_reg(DWC2_HCINT(ch), 0x7FF); // Clear interrupts
    usb_write_reg(DWC2_HCTSIZ(ch), hctsiz);
    usb_write_reg(DWC2_HCCHAR(ch), hcchar | (1 << 31)); // Enable channel

    return usb_wait_ch(ch);
}

static int usb_control_transfer(uint8_t addr, struct usb_setup_packet *setup, void *data) {
    /* SETUP Stage */
    int ret = usb_transfer(CH_CONTROL, addr, 0, 0, 0, setup, sizeof(*setup), 3); // PID_SETUP (3)
    if (ret < 0) return ret;

    /* DATA Stage (if any) */
    if (setup->wLength > 0) {
        uint8_t dir = (setup->bmRequestType & 0x80) ? 1 : 0;
        ret = usb_transfer(CH_CONTROL, addr, 0, 0, dir, data, setup->wLength, 2); // PID_DATA1 (2)
        if (ret < 0) return ret;
    }

    /* STATUS Stage */
    uint8_t status_dir = (setup->bmRequestType & 0x80) ? 0 : 1;
    ret = usb_transfer(CH_CONTROL, addr, 0, 0, status_dir, NULL, 0, 2); // PID_DATA1 (2)
    
    return ret;
}

/* Enumeration Logic */
static void usb_enumerate(void) {
    uart_puts("[usb] Enumerating device...\n");
    
    /* 1. Reset Port */
    uint32_t hprt = usb_read_reg(DWC2_HPRT);
    hprt &= ~((1<<1)|(1<<2)|(1<<5)); // W1C bits
    hprt |= (1 << 8); // PrtRst
    usb_write_reg(DWC2_HPRT, hprt);
    usb_delay(50);
    hprt &= ~(1 << 8);
    usb_write_reg(DWC2_HPRT, hprt);
    usb_delay(20);

    /* 2. Get Device Descriptor (partial) to find MaxPacket0 */
    struct usb_setup_packet setup;
    struct usb_device_descriptor desc;
    
    setup.bmRequestType = 0x80;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8);
    setup.wIndex = 0;
    setup.wLength = 8;
    
    if (usb_control_transfer(0, &setup, &desc) < 0) {
        uart_puts("[usb] Failed to get device descriptor\n");
        return;
    }
    usb_max_pkt0 = desc.bMaxPacketSize0;

    /* 3. Set Address */
    setup.bmRequestType = 0x00;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = 1;
    setup.wIndex = 0;
    setup.wLength = 0;
    
    if (usb_control_transfer(0, &setup, NULL) < 0) {
        uart_puts("[usb] SetAddress failed\n");
        return;
    }
    usb_dev_addr = 1;
    usb_delay(10);

    /* 4. Set Configuration 1 */
    setup.bmRequestType = 0x00;
    setup.bRequest = USB_REQ_SET_CONFIG;
    setup.wValue = 1;
    setup.wIndex = 0;
    setup.wLength = 0;
    
    if (usb_control_transfer(usb_dev_addr, &setup, NULL) < 0) {
        uart_puts("[usb] SetConfig failed\n");
        return;
    }

    /* 5. Set HID Boot Protocol (Keyboard) */
    setup.bmRequestType = 0x21; // Class, Interface
    setup.bRequest = HID_SET_PROTOCOL;
    setup.wValue = 0; // Boot Protocol
    setup.wIndex = 0; // Interface 0
    setup.wLength = 0;
    
    if (usb_control_transfer(usb_dev_addr, &setup, NULL) < 0) {
        // Many keyboards default to boot protocol, failure is okay-ish
    }

    /* 6. Set Idle 0 */
    setup.bmRequestType = 0x21;
    setup.bRequest = HID_SET_IDLE;
    setup.wValue = 0;
    setup.wIndex = 0;
    setup.wLength = 0;
    usb_control_transfer(usb_dev_addr, &setup, NULL);

    usb_has_keyboard = 1;
    usb_kb_ep_in = 1; // Assuming default HID keyboard EP
    usb_device_enumerated = 1;
    uart_puts("[usb] Keyboard Enumerated\n");
}

/* Key Scancode to Myras Keycode (Simplified) */
/* USB HID to PS/2 Set 1 Scan Code Mapping */
static const uint8_t usb_scan_map[] = {
    0, 0, 0, 0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, // 00-0F
    50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 2, 3, // 10-1F (A-Z, 1-2)
    4, 5, 6, 7, 8, 9, 10, 11, 28, 1, 14, 15, 57, 12, 13, 26, // 20-2F (3-0, Enter, Esc, Back, Tab, Space, -, =, [)
    27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64, // 30-3F (], \, \, ;, ', `, ,, ., /, Caps, F1-F5)
    65, 66, 67, 68, 87, 88, 70, 71, 110, 102, 104, 111, 107, 109, 106, 108, // 40-4F (F6-F12, PrtSc, Scrl, Pause, Ins, Home, PgUp, Del, End, PgDn, Rt)
    105, 103, 98, 69, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 72, 73, // 50-5F (Lt, Dn, Up, Num, /, *, -, +, Ent, 1-6)
    82, 71, 83, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 // 60-6F (7-9...)
};

static uint8_t usb_to_myras_scan(uint8_t usb_code) {
    if (usb_code < sizeof(usb_scan_map)) return usb_scan_map[usb_code];
    return 0;
}

static uint8_t usb_prev_keys[6] = {0};
static uint8_t usb_prev_modifiers = 0;

/* Simplified Modifier Mapping: LCtrl=0x1D, LShift=0x2A, LAlt=0x38, LGUI=0xDB */
static const uint8_t usb_mod_scans[] = { 0x1D, 0x2A, 0x38, 0xDB, 0x9D, 0x36, 0xB8, 0xDC };

void usb_poll(void) {
    if (!usb_initialized) return;

    /* 1. Check Connection */
    uint32_t hprt = usb_read_reg(DWC2_HPRT);
    if (hprt & (1 << 0)) { // PrtConnSts
        if (!usb_device_connected) {
            usb_device_connected = 1;
            usb_delay(100); 
            usb_enumerate();
        }
    } else {
        if (usb_device_connected) {
            usb_device_connected = 0;
            usb_device_enumerated = 0;
            usb_has_keyboard = 0;
            uart_puts("[usb] Device disconnected\n");
        }
    }

    /* 2. Poll Keyboard if active */
    if (usb_has_keyboard && usb_device_enumerated) {
        uint32_t now = scheduler_get_tick();
        if (now - usb_last_poll_tick < 10) return;
        usb_last_poll_tick = now;

        struct usb_keyboard_report report;
        int ret = usb_transfer(CH_INTR_IN, usb_dev_addr, usb_kb_ep_in, 3, 1, &report, sizeof(report), 2); // PID_DATA1 (2)
        
        if (ret == 0) { // Success
            /* Check Modifiers */
            for (int i = 0; i < 8; i++) {
                int was_pressed = (usb_prev_modifiers >> i) & 1;
                int is_pressed = (report.modifiers >> i) & 1;
                if (is_pressed != was_pressed) {
                    input_push_event(INPUT_TYPE_KEY, usb_mod_scans[i], is_pressed);
                }
            }
            usb_prev_modifiers = report.modifiers;

            /* Check Key Releases */
            for (int i = 0; i < 6; i++) {
                if (usb_prev_keys[i] == 0) continue;
                int still_pressed = 0;
                for (int j = 0; j < 6; j++) {
                    if (report.keycodes[j] == usb_prev_keys[i]) { still_pressed = 1; break; }
                }
                if (!still_pressed) {
                    uint8_t scan = usb_to_myras_scan(usb_prev_keys[i]);
                    if (scan) input_push_event(INPUT_TYPE_KEY, scan, 0);
                }
            }

            /* Check Key Presses */
            for (int i = 0; i < 6; i++) {
                if (report.keycodes[i] == 0) continue;
                int was_pressed = 0;
                for (int j = 0; j < 6; j++) {
                    if (usb_prev_keys[j] == report.keycodes[i]) { was_pressed = 1; break; }
                }
                if (!was_pressed) {
                    uint8_t scan = usb_to_myras_scan(report.keycodes[i]);
                    if (scan) input_push_event(INPUT_TYPE_KEY, scan, 1);
                }
            }
            
            for (int i = 0; i < 6; i++) usb_prev_keys[i] = report.keycodes[i];
        } else if (ret == -3) {
            // NAK is expected if no keys pressed
        }
    }
}

void usb_set_leds(uint8_t leds) {
    if (!usb_has_keyboard || !usb_device_enumerated) return;
    
    struct usb_setup_packet setup;
    setup.bmRequestType = 0x21; // Class, Interface
    setup.bRequest = HID_SET_REPORT;
    setup.wValue = (0x02 << 8); // Output Report
    setup.wIndex = 0;
    setup.wLength = 1;
    
    usb_current_leds = leds;
    usb_control_transfer(usb_dev_addr, &setup, &usb_current_leds);
}

int usb_init(void) {
    if (usb_initialized) return 0;

    /* 1. Power on USB via Mailbox */
    mbox[0] = 8*4;
    mbox[1] = 0;
    mbox[2] = 0x28001; // SET_POWER
    mbox[3] = 8;
    mbox[4] = 8;
    mbox[5] = 3; // USB
    mbox[6] = 3; // ON + WAIT
    mbox[7] = 0;
    
    if (!mbox_call(8, (unsigned int *)mbox) || (mbox[6] & 2)) {
        return -1;
    }

    /* 2. Soft Reset */
    usb_write_reg(DWC2_GRSTCTL, (1 << 0));
    int timeout = 1000000;
    while (usb_read_reg(DWC2_GRSTCTL) & (1 << 0)) { if (--timeout == 0) return -2; }
    
    timeout = 1000000;
    while (!(usb_read_reg(DWC2_GRSTCTL) & (1 << 31))) { if (--timeout == 0) return -3; }

    /* 3. Force Host Mode */
    uint32_t gusbcfg = usb_read_reg(DWC2_GUSBCFG);
    gusbcfg |= (1 << 30);
    usb_write_reg(DWC2_GUSBCFG, gusbcfg);
    usb_delay(50);

    /* 4. AHB/DMA Config */
    usb_write_reg(DWC2_GAHBCFG, (1 << 0) | (1 << 5)); // GIntMsk | DMAEnable

    /* 5. Core Int Mask */
    usb_write_reg(DWC2_GINTMSK, (1 << 24) | (1 << 25)); // Port | HCh

    /* 6. Port Power */
    uint32_t hprt = usb_read_reg(DWC2_HPRT);
    hprt &= ~((1<<1)|(1<<2)|(1<<5)); // W1C
    hprt |= (1 << 12);
    usb_write_reg(DWC2_HPRT, hprt);

    usb_initialized = 1;
    uart_puts("[usb] Host ready\n");
    return 0;
}
