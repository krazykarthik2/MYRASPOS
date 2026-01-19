#include "virtio.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>
#include "framebuffer.h"
#include "lib.h"
#include "palloc.h"
#include <string.h>
#include "input.h"
#include "kmalloc.h"

/* Virtio-GPU Command Types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106

#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM        1
#define VIRTIO_INPUT_EV_KEY 0x01
#define VIRTIO_INPUT_EV_REL 0x02
#define VIRTIO_INPUT_EV_ABS 0x03
#define VIRTIO_INPUT_EV_SYN 0x00

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        struct virtio_gpu_rect rect;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct {
        uint64_t addr;
        uint32_t length;
        uint32_t padding;
    } entries[1];
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

static uintptr_t gpu_mmio_base = 0;
static uint32_t gpu_res_id = 1;
static uint32_t gpu_scanout_id = 0;
static int gpu_active = 0;
static uint32_t gpu_w = 0, gpu_h = 0;
static uint8_t *gpu_qmem = NULL;
static volatile int gpu_lock = 0;

#define MAX_INPUT_DEVICES 4
struct virtio_input_state {
    uintptr_t mmio_base;
    uint8_t *qmem;
    struct virtio_input_event *ev_buf;
    uint32_t qsize;
    uint16_t last_used_idx;
};

static struct virtio_input_state input_devs[MAX_INPUT_DEVICES];
static int num_input_devs = 0;

static void gpu_acquire_lock() {
    unsigned int tmp;
    __asm__ volatile(
        "1: ldxr %w0, [%1]\n"          // Load exclusive
        "   cbnz %w0, 1b\n"            // If not 0, try again
        "   stxr %w0, %w2, [%1]\n"     // Store 1 exclusive
        "   cbnz %w0, 1b\n"            // If store failed, try again
        "   dmb sy\n"                  // Data memory barrier
        : "=&r" (tmp)
        : "r" (&gpu_lock), "r" (1)
        : "memory"
    );
}

static void gpu_release_lock() {
    __asm__ volatile(
        "dmb sy\n"                     // Data memory barrier
        "str wzr, [%0]\n"              // Store 0 to lock
        : : "r" (&gpu_lock) : "memory"
    );
}

static void virtio_flush_dcache(void *start, size_t size) {
    uintptr_t start_addr = (uintptr_t)start & ~(uintptr_t)63;
    uintptr_t end_addr = (uintptr_t)start + size;
    
    for (uintptr_t p = start_addr; p < end_addr; p += 64) {
        __asm__ volatile("dc civac, %0" : : "r" (p) : "memory");
    }
    __asm__ volatile("dmb sy" ::: "memory");
}

static int virtio_gpu_send_command(void *req, size_t req_len, void *resp, size_t resp_len) {
    if (!gpu_mmio_base || !gpu_qmem) return -1;
    gpu_acquire_lock();
    #define R_GPU(off) ((volatile uint32_t *)(gpu_mmio_base + (off)))

    /* Use the same memory area for descriptors and rings */
    uint32_t avail_offset = 16 * 16; /* qsize=16 */
    uint32_t used_offset = 4096;
    uint32_t req_data_off = 4096 + 512;
    uint32_t resp_data_off = req_data_off + 1024;

    memcpy(gpu_qmem + req_data_off, req, req_len);
    memset(gpu_qmem + resp_data_off, 0, resp_len);

    uintptr_t phys = (uintptr_t)gpu_qmem;
    volatile uint8_t *dtab = gpu_qmem;
    
    /* Use 2 descriptors: 0=Request (Header+Data), 1=Response */
    /* desc0: Request (Header + any parameters) */
    *(uint64_t *)(dtab + 0) = phys + req_data_off;
    *(uint32_t *)(dtab + 8) = (uint32_t)req_len;
    *(uint16_t *)(dtab + 12) = 1; /* VIRTQ_DESC_F_NEXT */
    *(uint16_t *)(dtab + 14) = 1;

    /* desc1: Response */
    *(uint64_t *)(dtab + 16) = phys + resp_data_off;
    *(uint32_t *)(dtab + 24) = (uint32_t)resp_len;
    *(uint16_t *)(dtab + 28) = 2; /* VIRTQ_DESC_F_WRITE */
    *(uint16_t *)(dtab + 30) = 0;

    /* avail ring */
    volatile uint16_t *avail = (volatile uint16_t *)(gpu_qmem + avail_offset);
    uint16_t idx = avail[1];
    avail[2 + (idx % 16)] = 0; /* head of chain is always desc 0 */
    __asm__ volatile("dmb sy" ::: "memory");
    avail[1] = idx + 1;
    __asm__ volatile("dmb sy" ::: "memory");

    uint32_t cmd_type = *(uint32_t *)req;
    (void)cmd_type;

    *R_GPU(0x050) = 0; /* notify */
    __asm__ volatile("dmb sy" ::: "memory");

    /* wait for used */
    volatile uint16_t *used = (volatile uint16_t *)(gpu_qmem + used_offset);
    int loops = 0;
    while (used[1] == idx && loops < 20000000) { loops++; }
    if (loops >= 20000000) {
        uart_puts("[virtio] command timeout (cmd=");
        uart_put_hex(*(uint32_t *)req); uart_puts(")\n");
        gpu_release_lock();
        return -1;
    }
    __asm__ volatile("dmb sy" ::: "memory");

    memcpy(resp, gpu_qmem + resp_data_off, resp_len);
    gpu_release_lock();
    return 0;
}

/*
 * Minimal virtio helpers (placeholder)
 *
 * This file intentionally implements a stubbed virtio layer that prints
 * informative debug messages and returns -1 (not available). It is
 * structured so we can incrementally add virtio-pci or virtio-mmio
 * probing and negotiation code without changing the callers.
 */

int virtio_init(void) {
    uart_puts("[virtio] virtio_init: probe start\n");
    /* No platform-wide virtio enumeration implemented yet. */
    uart_puts("[virtio] virtio_init: no virtio bus support (stub)\n");
    return -1;
}

int virtio_gpu_init(void) {
    uart_puts("[virtio] virtio_gpu_init: attempting to initialize virtio-gpu\n");
    /*
     * TODO: implement the following steps:
     * 1) Probe PCI/virtio bus for a device with vendor/device IDs
     *    matching virtio-gpu or via virtio-mmio magic.
     * 2) Negotiate features and set up virtqueues.
     * 3) Allocate a scanout/backing buffer and inform the device.
     * 4) Map the guest-visible framebuffer and call fb_init(addr,width,height,stride).
     *
     * For now return -1 so callers fall back to RAMFB or other paths.
     */
        /* Implement virtio-mmio transport probe and minimal virtqueue + GET_DISPLAY_INFO
         * Steps:
         *  - Probe MMIO registers at VIRTIO_GPU_MMIO_BASE
         *  - Verify magic/version/device/vendor
         *  - Negotiate zero features and set FEATURES_OK
         *  - Set up one virtqueue (queue 0)
         *  - Send GET_DISPLAY_INFO control command and wait for response
         */

    uart_puts("[virtio] virtio_gpu_init: searching for virtio-gpu\n");
    uintptr_t found_base = 0;
    for (int i = 0; i < 32; i++) {
        uintptr_t base = 0x0A000000UL + i * 0x200;
        volatile uint32_t *r = (volatile uint32_t *)base;
        if (r[0] != 0x74726976u) continue; // magic "virt"
        
        uint32_t dev_id = r[2]; // offset 0x08
        uint32_t version = r[1]; // offset 0x04
        uart_puts("[virtio] slot "); uart_put_hex(i); 
        uart_puts(": dev="); uart_put_hex(dev_id); 
        uart_puts(" ver="); uart_put_hex(version); uart_puts("\n");

        if (dev_id == 16u) {
            found_base = base;
            uart_puts("[virtio] found virtio-gpu at 0x"); uart_put_hex(found_base); uart_puts("\n");
            break;
        }
    }

    if (!found_base) {
        uart_puts("[virtio] virtio-gpu not found\n");
        return -1;
    }

    const uintptr_t VIRTIO_GPU_MMIO_BASE = found_base;
    #define R(off) ((volatile uint32_t *)(VIRTIO_GPU_MMIO_BASE + (off)))

    uint32_t version = *R(0x004);
    
    /* Reset device */
    *R(0x070) = 0;

    /* Acknowledge and Driver bits */
    *R(0x070) = 1; /* ACKNOWLEDGE */
    *R(0x070) |= 2; /* DRIVER */

    /* basic feature negotiation: accept none */
    *R(0x014) = 0; /* DEVICE_FEATURES_SEL = 0 */
    uint32_t dev_features = *R(0x010);
    (void)dev_features;
    *R(0x024) = 0; /* DRIVER_FEATURES_SEL = 0 */
    *R(0x020) = 0; /* DRIVER_FEATURES = 0 */

    if (version >= 2) {
        /* set FEATURES_OK for modern */
        *R(0x070) |= 8; /* FEATURES_OK */
        uint32_t status = *R(0x070);
        if (!(status & 8u)) { uart_puts("[virtio] FEATURES_OK not accepted\n"); return -1; }
    }

    /* virtqueue setup: use queue 0 */
    *R(0x030) = 0; /* QUEUE_SEL = 0 */
    uint32_t qmax = *R(0x034); /* QUEUE_NUM_MAX */
    if (qmax == 0) { uart_puts("[virtio] queue 0 not available\n"); return -1; }
    uint32_t qsize = qmax < 16 ? qmax : 16;
    *R(0x038) = qsize; /* QUEUE_NUM */

    /* Use a static aligned buffer for the descriptors and rings to ensure proper alignment and avoid palloc limits */
    static uint8_t queue_mem[8192] __attribute__((aligned(4096)));
    memset(queue_mem, 0, sizeof(queue_mem));
    uintptr_t phys = (uintptr_t)queue_mem;

    /* layout for legacy: 
       desc table at 0
       avail ring at 16 * qsize
       used ring at PAGE_SIZE (if QueueAlign is 4096)
    */
    uint32_t desc_size = qsize * 16;
    uint32_t avail_offset = desc_size;
    uint32_t used_offset = 4096; /* aligned to 4096 */

    if (version >= 2) {
        uint32_t desc_addr_low = (uint32_t)(phys & 0xFFFFFFFFu);
        uint32_t desc_addr_high = (uint32_t)((phys >> 32) & 0xFFFFFFFFu);
        uint32_t avail_addr_low = (uint32_t)((phys + avail_offset) & 0xFFFFFFFFu);
        uint32_t avail_addr_high = (uint32_t)(((phys + avail_offset) >> 32) & 0xFFFFFFFFu);
        uint32_t used_addr_low = (uint32_t)((phys + used_offset) & 0xFFFFFFFFu);
        uint32_t used_addr_high = (uint32_t)(((phys + used_offset) >> 32) & 0xFFFFFFFFu);

        *R(0x080) = desc_addr_low;
        *R(0x084) = desc_addr_high;
        *R(0x090) = avail_addr_low;
        *R(0x094) = avail_addr_high;
        *R(0x0a0) = used_addr_low;
        *R(0x0a4) = used_addr_high;

        *R(0x044) = 1; /* QUEUE_READY = 1 */
    } else {
        /* Legacy MMIO queue setup */
        *R(0x028) = 4096; /* GuestPageSize */
        *R(0x03c) = 4096; /* QueueAlign */
        *R(0x040) = (uint32_t)(phys / 4096);
    }

    /* DRIVER_OK */
    *R(0x070) |= 4;

    uart_puts("[virtio] virtqueue set: qsize="); uart_put_hex(qsize); uart_puts(" phys=0x"); uart_put_hex((uint32_t)(phys & 0xffffffffu)); uart_puts("\n");

        gpu_mmio_base = VIRTIO_GPU_MMIO_BASE;
        gpu_qmem = queue_mem;

        /* GET_DISPLAY_INFO */
        struct virtio_gpu_ctrl_hdr gdi_cmd;
        memset(&gdi_cmd, 0, sizeof(gdi_cmd));
        gdi_cmd.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
        
        struct virtio_gpu_resp_display_info gdi_resp;
        if (virtio_gpu_send_command(&gdi_cmd, sizeof(gdi_cmd), &gdi_resp, sizeof(gdi_resp)) < 0) {
            uart_puts("[virtio] display info command failed\n");
            return -1;
        }

        if (gdi_resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
            uart_puts("[virtio] display info response error (type=");
            uart_put_hex(gdi_resp.hdr.type); uart_puts(")\n");
            return -1;
        }

        gpu_w = 0; gpu_h = 0;
        for (int i = 0; i < 16; i++) {
            if (gdi_resp.pmodes[i].enabled) {
                gpu_scanout_id = i;
                gpu_w = gdi_resp.pmodes[i].rect.width;
                gpu_h = gdi_resp.pmodes[i].rect.height;
                uart_puts("[virtio] found enabled scanout: "); 
                uart_put_hex(gpu_w); uart_puts("x"); uart_put_hex(gpu_h); uart_puts("\n");
                break;
            }
        }

        if (gpu_w == 0) {
            uart_puts("[virtio] no enabled scanout reported; defaulting to 800x600 scanout 0\n");
            gpu_w = 800;
            gpu_h = 600;
            gpu_scanout_id = 0;
            
            /* Print first few words of response for debug */
            uint32_t *raw = (uint32_t *)&gdi_resp;
            uart_puts("[virtio] raw response: ");
            for(int k=0; k<8; k++) { uart_put_hex(raw[k]); uart_puts(" "); }
            uart_puts("\n");
        }

        /* RESOURCE_CREATE_2D */
        struct virtio_gpu_resource_create_2d rc_cmd;
        memset(&rc_cmd, 0, sizeof(rc_cmd));
        rc_cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
        rc_cmd.resource_id = gpu_res_id;
        rc_cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
        rc_cmd.width = gpu_w;
        rc_cmd.height = gpu_h;

        struct virtio_gpu_ctrl_hdr gen_resp;
        memset(&gen_resp, 0, sizeof(gen_resp));
        int rc_err = virtio_gpu_send_command(&rc_cmd, sizeof(rc_cmd), &gen_resp, sizeof(gen_resp));
        if (rc_err < 0 || gen_resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
            uart_puts("[virtio] resource create failed (err=");
            uart_put_hex(rc_err); uart_puts(" resp=");
            uart_put_hex(gen_resp.type); uart_puts(")\n");
            return -1;
        }

        /* RESOURCE_ATTACH_BACKING */
        struct virtio_gpu_resource_attach_backing ab_cmd;
        memset(&ab_cmd, 0, sizeof(ab_cmd));
        ab_cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
        ab_cmd.resource_id = gpu_res_id;
        ab_cmd.nr_entries = 1;
        ab_cmd.entries[0].addr = 0x42000000; /* 32MB in - absolutely clear of any boot structs */
        ab_cmd.entries[0].length = gpu_w * gpu_h * 4;

        if (virtio_gpu_send_command(&ab_cmd, sizeof(ab_cmd), &gen_resp, sizeof(gen_resp)) < 0 || gen_resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
            uart_puts("[virtio] attach backing failed\n");
            return -1;
        }

        /* SET_SCANOUT */
        struct virtio_gpu_set_scanout ss_cmd;
        memset(&ss_cmd, 0, sizeof(ss_cmd));
        ss_cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
        ss_cmd.resource_id = gpu_res_id;
        ss_cmd.scanout_id = gpu_scanout_id;
        ss_cmd.r.x = 0; ss_cmd.r.y = 0;
        ss_cmd.r.width = gpu_w; ss_cmd.r.height = gpu_h;

        if (virtio_gpu_send_command(&ss_cmd, sizeof(ss_cmd), &gen_resp, sizeof(gen_resp)) < 0 || gen_resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
            uart_puts("[virtio] set scanout failed (resp=");
            uart_put_hex(gen_resp.type); uart_puts(")\n");
            /* Continue anyway as some devices might accept it later or fail but work */
        }

        gpu_active = 1;
        uart_puts("[virtio] GPU status: active at "); 
        uart_put_hex(gpu_w); uart_puts("x"); uart_put_hex(gpu_h);
        uart_puts(" res_id="); uart_put_hex(gpu_res_id);
        uart_puts(" scanout="); uart_put_hex(gpu_scanout_id); uart_puts("\n");

        return 0;
}

void virtio_gpu_flush(void) {
    if (!gpu_active) return;

    struct virtio_gpu_transfer_to_host_2d th_cmd;
    memset(&th_cmd, 0, sizeof(th_cmd));
    th_cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    th_cmd.resource_id = gpu_res_id;
    th_cmd.r.x = 0; th_cmd.r.y = 0;
    th_cmd.r.width = gpu_w; th_cmd.r.height = gpu_h;
    th_cmd.offset = 0;

    struct virtio_gpu_ctrl_hdr gen_resp;
    if (virtio_gpu_send_command(&th_cmd, sizeof(th_cmd), &gen_resp, sizeof(gen_resp)) < 0 || gen_resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        return;
    }

    struct virtio_gpu_resource_flush fl_cmd;
    memset(&fl_cmd, 0, sizeof(fl_cmd));
    fl_cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    fl_cmd.resource_id = gpu_res_id;
    fl_cmd.r.x = 0; fl_cmd.r.y = 0;
    fl_cmd.r.width = gpu_w; fl_cmd.r.height = gpu_h;

    virtio_gpu_send_command(&fl_cmd, sizeof(fl_cmd), &gen_resp, sizeof(gen_resp));
}



int virtio_input_init(void) {
    uart_puts("[virtio] searching for virtio-input devices...\n");
    num_input_devs = 0;

    for (int i = 0; i < 32 && num_input_devs < MAX_INPUT_DEVICES; i++) {
        uintptr_t base = 0x0A000000UL + i * 0x200;
        volatile uint32_t *r = (volatile uint32_t *)base;
        if (r[0] != 0x74726976u) continue;
        if (r[2] == 18u) { // dev_id 18 = input
            uint32_t version = r[1];
            uart_puts("[virtio] found virtio-input (v");
            uart_put_hex(version);
            uart_puts(") at 0x"); 
            uart_put_hex(base); uart_puts("\n");

            struct virtio_input_state *dev = &input_devs[num_input_devs];
            dev->mmio_base = base;
            
            #define RI_INIT(off) ((volatile uint32_t *)(base + (off)))
            
            uart_puts("[virtio] input: reset...\n");
            *RI_INIT(0x070) = 0; 
            for(volatile int d=0; d<10000; d++); /* delay */
            *RI_INIT(0x070) = 1; /* ACKNOWLEDGE */
            *RI_INIT(0x070) |= 2; /* DRIVER */
            for(volatile int d=0; d<10000; d++); /* delay */

            /* Feature Negotiation */
            *RI_INIT(0x014) = 0; /* DEVICE_FEATURES_SEL = 0 */
            uint32_t features = *RI_INIT(0x010);
            (void)features; // read to ack
            
            *RI_INIT(0x024) = 0; /* DRIVER_FEATURES_SEL = 0 */
            *RI_INIT(0x020) = 0; /* DRIVER_FEATURES = 0 (we don't use any yet) */

            uart_puts("[virtio] input: features OK check...\n");
            if (version >= 2) {
                *RI_INIT(0x070) |= 8; /* FEATURES_OK */
                for(volatile int d=0; d<10000; d++); /* delay */
                if (!(*RI_INIT(0x070) & 8)) {
                    uart_puts("[virtio] input FEATURES_OK failed\n");
                    continue;
                }
            }

            /* ISOLATION TEST 2 REVERTED: QUEUE SETUP ENABLED */
            // goto skip_setup; 
            
            /* QUEUE 0 */
            *RI_INIT(0x030) = 0;
            uint32_t qmax = *RI_INIT(0x034);
            if (qmax == 0) {
                uart_puts("[virtio] input device has no queue 0\n");
                continue;
            }
            dev->qsize = qmax < 32 ? qmax : 32;
            *RI_INIT(0x038) = dev->qsize;


            /* Use Static Memory for Queues (Like GPU) to avoid palloc/alignment/coherence issues */
            static uint8_t input_queues_static[MAX_INPUT_DEVICES][8192] __attribute__((aligned(4096)));
            
            dev->qmem = input_queues_static[num_input_devs];
            memset(dev->qmem, 0, 8192);
            
            uintptr_t phys = (uintptr_t)dev->qmem;
            uart_puts("[virtio] input queue (8KB) static at: "); uart_put_hex(phys); uart_puts("\n");

            if (version >= 2) {
                *RI_INIT(0x080) = (uint32_t)phys;
                *RI_INIT(0x084) = (uint32_t)(phys >> 32);
                *RI_INIT(0x090) = (uint32_t)(phys + dev->qsize * 16);
                *RI_INIT(0x094) = (uint32_t)((phys + dev->qsize * 16) >> 32);
                *RI_INIT(0x0a0) = (uint32_t)(phys + 4096);
                *RI_INIT(0x0a4) = (uint32_t)((phys + 4096) >> 32);
                *RI_INIT(0x044) = 1; /* READY */
            } else {
                *RI_INIT(0x028) = 4096; /* GuestPageSize */
                *RI_INIT(0x03c) = 4096; /* QueueAlign (Standard 4KB) */
                *RI_INIT(0x040) = (uint32_t)(phys / 4096);
            }

            dev->ev_buf = kmalloc(sizeof(struct virtio_input_event) * dev->qsize);
            if (!dev->ev_buf) {
                uart_puts("[virtio] failed to allocate event buffer\n");
                continue;
            }
            dev->last_used_idx = 0;
            


            /* RESTORED: POPULATE QUEUES */
            for (uint32_t j = 0; j < dev->qsize; j++) {
                uint8_t *dt = dev->qmem + j * 16;
                *(uint64_t *)dt = (uintptr_t)&dev->ev_buf[j];
                *(uint32_t *)(dt + 8) = 32; 
                *(uint16_t *)(dt + 12) = 2; /* VIRTQ_DESC_F_WRITE */
                *(uint16_t *)(dt + 14) = 0;
                
                volatile uint16_t *avail = (volatile uint16_t *)(dev->qmem + dev->qsize * 16);
                avail[2 + j] = (uint16_t)j;
            }
            
            volatile uint16_t *avail = (volatile uint16_t *)(dev->qmem + dev->qsize * 16);
            avail[0] = 1; /* NO_INTERRUPT */
            __asm__ volatile("dmb sy" ::: "memory");
            avail[1] = (uint16_t)dev->qsize;
            __asm__ volatile("dmb sy" ::: "memory");
            
            /* CRITICAL: Flush cache so QEMU sees the descriptors */
            virtio_flush_dcache(dev->qmem, 8192);

            /* VERIFY READBACK */
            __asm__ volatile("dmb sy" ::: "memory");
            uint8_t *check_dt = dev->qmem;
            uint32_t check_len = *(volatile uint32_t *)(check_dt + 8);
            uart_puts("[virtio] debug: desc[0].len = "); uart_put_hex(check_len); uart_puts("\n");

            *RI_INIT(0x050) = 0; /* notify */
            
            uart_puts("[virtio] input setting DRIVER_OK...\n");
            
            /* Defensive: ACK any pending interrupts before enabling driver */
            volatile uint32_t irq_status = *RI_INIT(0x060);
            (void)irq_status;
            *RI_INIT(0x064) = 0x3; /* ACK bits 0 and 1 (VRING and CONFIG) */
            
            /* Explicit Read-Modify-Write for Status */
            uint32_t status = *RI_INIT(0x070);
            *RI_INIT(0x070) = status | 4; /* DRIVER_OK */
            
            uart_puts("[virtio] DRIVER_OK set. Input active.\n");

            num_input_devs++;
            #undef RI_INIT
        }
    }
    uart_puts("[virtio] input init done, devs="); uart_put_hex(num_input_devs); uart_puts("\n");
    return (num_input_devs > 0) ? 0 : -1;
}

void virtio_input_poll(void) {
    for (int i = 0; i < num_input_devs; i++) {
        struct virtio_input_state *dev = &input_devs[i];
        if (!dev->qmem) continue;

        if (!dev->qmem) continue;
        
        /* Used Ring at offset 4096 (Standard Legacy) */
        volatile uint16_t *used = (volatile uint16_t *)(dev->qmem + 4096);
        uint16_t used_idx = used[1];
        
        static int debug_counter = 0;
        if (i == 1 && ((++debug_counter) % 1000 == 0)) { // Log mouse (usually dev 1) occasionally
             // uart_puts("[virtio] poll dev1 used_idx="); uart_put_hex(used_idx); 
             // uart_puts(" last="); uart_put_hex(dev->last_used_idx); uart_puts("\n");
        }

        while (dev->last_used_idx != used_idx) {
            uint32_t used_ring_off = 4 + (dev->last_used_idx % dev->qsize) * 8;
            uint32_t desc_idx = *(uint32_t *)((uint8_t *)used + used_ring_off);
            
            struct virtio_input_event *ev = &dev->ev_buf[desc_idx];
            
            /* Debug: see events in UART */
            // uart_puts("[input] dev="); uart_put_hex(i); 
            // uart_puts(" type="); uart_put_hex(ev->type);
            // uart_puts(" code="); uart_put_hex(ev->code);
            // uart_puts(" val="); uart_put_hex(ev->value); uart_puts("\n");
            
            /* Map virtio-input to our internal types */
            if (ev->type == VIRTIO_INPUT_EV_KEY) {
                input_push_event(INPUT_TYPE_KEY, ev->code, (int32_t)ev->value);
            } else if (ev->type == VIRTIO_INPUT_EV_ABS) {
                input_push_event(INPUT_TYPE_ABS, ev->code, (int32_t)ev->value);
            } else if (ev->type == VIRTIO_INPUT_EV_REL) {
                input_push_event(INPUT_TYPE_REL, ev->code, (int32_t)ev->value);
            }
            
            /* Put descriptor back on avail ring */
            volatile uint16_t *avail = (volatile uint16_t *)(dev->qmem + dev->qsize * 16);
            uint16_t a_idx = avail[1];
            avail[2 + (a_idx % dev->qsize)] = (uint16_t)desc_idx;
            __asm__ volatile("dmb sy" ::: "memory");
            avail[1] = a_idx + 1;
            
            dev->last_used_idx++;
            
            #define RI_POLL(off) ((volatile uint32_t *)(dev->mmio_base + (off)))
            *RI_POLL(0x050) = 0; /* notify */
            #undef RI_POLL
        }
    }
}


int virtio_gpu_get_width(void) { return gpu_w; }
int virtio_gpu_get_height(void) { return gpu_h; }