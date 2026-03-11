#ifndef USB_H
#define USB_H

#include <stdint.h>

/*
 * Simplified DWC2 USB Host Driver for Myras
 * Targets: Raspberry Pi Zero 2W (BCM2710 / BCM2837)
 */

#define USB_BASE 0x3F980000ULL

/* Global Registers */
#define DWC2_GOTGCTL    0x000
#define DWC2_GOTGINT    0x004
#define DWC2_GAHBCFG    0x008
#define DWC2_GUSBCFG    0x00C
#define DWC2_GRSTCTL    0x010
#define DWC2_GINTSTS    0x014
#define DWC2_GINTMSK    0x018
#define DWC2_GRXSTSR    0x01C
#define DWC2_GRXSTSP    0x020
#define DWC2_GRXFSIZ    0x024
#define DWC2_GNPTXFSIZ  0x028
#define DWC2_GUID       0x03C
#define DWC2_GSNPSID    0x040
#define DWC2_GHWCFG1    0x044
#define DWC2_GHWCFG2    0x048
#define DWC2_GHWCFG3    0x04C
#define DWC2_GHWCFG4    0x050

/* Host Registers */
#define DWC2_HCFG       0x400
#define DWC2_HFIR       0x404
#define DWC2_HFNUM      0x408
#define DWC2_HAINT      0x414
#define DWC2_HAINTMSK   0x418
#define DWC2_HPRT       0x440

/* Host Channel Registers (CH 0-15) */
#define DWC2_HCCHAR(n)    (0x500 + (n) * 0x20)
#define DWC2_HCINT(n)     (0x508 + (n) * 0x20)
#define DWC2_HCINTMSK(n)  (0x50C + (n) * 0x20)
#define DWC2_HCTSIZ(n)    (0x510 + (n) * 0x20)
#define DWC2_HCDMA(n)     (0x514 + (n) * 0x20)
#define DWC2_PCGCCTL      0xE00

/* Standard USB Request Types */
#define USB_REQ_GET_STATUS     0x00
#define USB_REQ_CLEAR_FEATURE  0x01
#define USB_REQ_SET_FEATURE    0x03
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIG     0x08
#define USB_REQ_SET_CONFIG     0x09
#define USB_REQ_SET_INTERFACE  0x0B

#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIG        0x02
#define USB_DESC_STRING        0x03
#define USB_DESC_INTERFACE     0x04
#define USB_DESC_ENDPOINT      0x05

/* HID Specific */
#define USB_DESC_HID           0x21
#define USB_DESC_REPORT        0x22
#define HID_SET_REPORT         0x09
#define HID_SET_IDLE           0x0A
#define HID_SET_PROTOCOL       0x0B

struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

/* USB Keyboard Report (HID Boot Protocol) */
struct usb_keyboard_report {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycodes[6];
} __attribute__((packed));

/* USB Mouse Report (HID Boot Protocol) */
struct usb_mouse_report {
    uint8_t buttons;  /* bit0=L, bit1=R, bit2=Middle */
    int8_t  x;        /* relative X movement */
    int8_t  y;        /* relative Y movement */
    int8_t  wheel;    /* scroll wheel */
} __attribute__((packed));

/* Controller API */
int usb_init(void);
void usb_poll(void);
void usb_set_leds(uint8_t leds);

#endif
