# VirtIO Drivers Documentation

**File**: `kernel/virtio.c`, `kernel/virtio.h`  
**Purpose**: VirtIO GPU, block device, and input device drivers for MYRASPOS  
**Status**: Production - Fully functional VirtIO-MMIO implementation

---

## Table of Contents

1. [Overview](#overview)
2. [VirtIO Specification](#virtio-specification)
3. [Data Structures](#data-structures)
4. [GPU Driver](#gpu-driver)
5. [Block Driver](#block-driver)
6. [Input Driver](#input-driver)
7. [Key Functions](#key-functions)
8. [Implementation Details](#implementation-details)
9. [Design Decisions](#design-decisions)
10. [Hardware Addresses](#hardware-addresses)
11. [Usage Examples](#usage-examples)
12. [Cross-References](#cross-references)

---

## Overview

### VirtIO Architecture

MYRASPOS implements VirtIO version 1.0/2.0 drivers using the **VirtIO-MMIO** transport mechanism. VirtIO provides a standardized interface for virtual devices in QEMU and other hypervisors, offering better performance and simplicity compared to emulated legacy hardware.

### Supported Device Types

1. **VirtIO-GPU (Device ID 16)**: Graphics adapter providing 2D framebuffer
2. **VirtIO-Block (Device ID 2)**: Block storage device for disk I/O
3. **VirtIO-Input (Device ID 18)**: Keyboard and mouse input devices

### Why VirtIO?

- **Performance**: Direct virtqueue communication reduces emulation overhead
- **Simplicity**: Well-defined specification with minimal legacy cruft
- **Portability**: Works across QEMU, KVM, and other modern hypervisors
- **Modern Design**: Clean separation between transport (MMIO) and device logic

---

## VirtIO Specification

### VirtIO-MMIO Transport

VirtIO-MMIO devices are discovered by probing memory-mapped I/O regions. MYRASPOS scans **0x0A000000-0x0A003E00** (32 slots × 0x200 bytes).

#### Device Detection

```c
// Magic value at offset 0x00
#define VIRTIO_MAGIC 0x74726976  // "virt"

// Device IDs (offset 0x08)
#define VIRTIO_ID_BLOCK  2
#define VIRTIO_ID_GPU    16
#define VIRTIO_ID_INPUT  18
```

### VirtQueue Architecture

VirtQueues are the core communication mechanism between guest (MYRASPOS) and host (QEMU).

#### VirtQueue Components

1. **Descriptor Table**: Array of buffer descriptors
2. **Available Ring**: Guest-to-host notification ring
3. **Used Ring**: Host-to-guest completion ring

#### Descriptor Format

```c
struct virtq_desc {
    uint64_t addr;    // Physical address of buffer
    uint32_t len;     // Buffer length
    uint16_t flags;   // NEXT | WRITE
    uint16_t next;    // Index of next descriptor in chain
};
```

**Flags**:
- `VIRTQ_DESC_F_NEXT (1)`: More descriptors follow
- `VIRTQ_DESC_F_WRITE (2)`: Device writes to buffer (guest reads)

#### Queue Memory Layout

```
Offset 0:              Descriptor Table (qsize × 16 bytes)
Offset qsize × 16:     Available Ring (6 + qsize × 2 bytes)
Offset 4096:           Used Ring (6 + qsize × 8 bytes)
Offset 4096+512:       Data buffers (implementation-specific)
```

### Device Initialization Sequence

1. **Reset** device (Status = 0)
2. **Acknowledge** device (Status |= 1)
3. Set **Driver** bit (Status |= 2)
4. **Feature negotiation** (read DEVICE_FEATURES, write DRIVER_FEATURES)
5. Set **Features OK** (Status |= 8) for modern devices
6. **Queue setup** (configure descriptor/available/used addresses)
7. Set **Driver OK** (Status |= 4)

---

## Data Structures

### GPU Command Structures

```c
// Command types
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106

// Response types
#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101

// Common header for all GPU commands
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

// Rectangle specification
struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

// Display information response
struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        struct virtio_gpu_rect rect;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
} __attribute__((packed));

// Resource creation
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

// Backing memory attachment
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

// Scanout configuration
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

// Transfer framebuffer data to host
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

// Flush display updates
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));
```

### Block Device Structures

```c
#define VIRTIO_BLK_T_IN   0  // Read from device
#define VIRTIO_BLK_T_OUT  1  // Write to device

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_status {
    uint8_t status;  // 0 = success
} __attribute__((packed));
```

### Input Device Structures

```c
#define VIRTIO_INPUT_EV_KEY  0x01  // Keyboard event
#define VIRTIO_INPUT_EV_REL  0x02  // Relative movement (mouse)
#define VIRTIO_INPUT_EV_ABS  0x03  // Absolute position (touchscreen/tablet)
#define VIRTIO_INPUT_EV_SYN  0x00  // Synchronization marker

struct virtio_input_event {
    uint16_t type;   // Event type (KEY/REL/ABS/SYN)
    uint16_t code;   // Key code or axis
    uint32_t value;  // Value (0/1 for keys, position for mouse)
} __attribute__((packed));
```

### Driver State Structures

```c
// Per-input-device state
struct virtio_input_state {
    uintptr_t mmio_base;            // MMIO register base address
    uint8_t *qmem;                  // VirtQueue memory
    struct virtio_input_event *ev_buf;  // Event buffer array
    uint32_t qsize;                 // Queue size
    uint16_t last_used_idx;         // Last processed used ring index
};

// Global state
static uintptr_t gpu_mmio_base = 0;
static uint32_t gpu_res_id = 1;
static uint32_t gpu_scanout_id = 0;
static int gpu_active = 0;
static uint32_t gpu_w = 0, gpu_h = 0;
static uint8_t *gpu_qmem = NULL;

static uintptr_t blk_mmio_base = 0;
static uint8_t *blk_qmem = NULL;

static struct virtio_input_state input_devs[MAX_INPUT_DEVICES];
static int num_input_devs = 0;
```

---

## GPU Driver

### Initialization Sequence

The GPU driver initialization follows these steps:

1. **Device Discovery**: Scan MMIO slots for device ID 16
2. **Device Reset and Handshake**: Standard VirtIO initialization
3. **Get Display Info**: Query available resolutions
4. **Create 2D Resource**: Allocate framebuffer resource
5. **Attach Backing**: Link physical memory to resource
6. **Set Scanout**: Configure output display

#### Framebuffer Setup

```c
int virtio_gpu_init(void);
```

**Process**:

1. Probes 32 MMIO slots starting at 0x0A000000
2. Verifies magic value 0x74726976 ("virt")
3. Finds device with ID 16 (GPU)
4. Resets device and negotiates zero features (no optional features needed)
5. Sets up queue 0 with 16 descriptors
6. Sends `GET_DISPLAY_INFO` command
7. Creates 2D resource with format `VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM` (BGRA32)
8. Attaches backing memory at **0x42000000** (physically mapped framebuffer)
9. Sets scanout to resource ID 1

**Memory Layout**:
- **Framebuffer**: 0x42000000 (32MB offset, clear of boot structures)
- **Size**: width × height × 4 bytes (BGRA32)

### Scanout and Resource Management

The GPU uses a **resource-based model**:

- **Resource**: GPU-side representation of a 2D image
- **Backing Memory**: Guest physical memory containing pixel data
- **Scanout**: Display output connected to a resource

### Flushing Updates

```c
void virtio_gpu_flush(void);
```

Two-step process to update display:

1. **TRANSFER_TO_HOST_2D**: Copy framebuffer changes from backing memory to GPU resource
2. **RESOURCE_FLUSH**: Signal GPU to update the actual display output

**Synchronization**:
- Uses spinlock (`gpu_lock`) for thread safety
- Disables interrupts during command sequences
- Timeout protection (1,000,000 iterations)

### GPU Command Sending

```c
static int virtio_gpu_send_command(void *req, size_t req_len, 
                                    void *resp, size_t resp_len);
```

**VirtQueue Protocol**:

1. Build 2-descriptor chain:
   - Descriptor 0: Request (command header + parameters)
   - Descriptor 1: Response (write-back buffer for GPU)
2. Place descriptor 0 index in available ring
3. Increment available index
4. Notify device (write to 0x050)
5. Poll used ring for completion
6. Copy response and return

**Cache Management**:
- Clean (writeback) descriptors and request data before notify
- Invalidate response buffer after completion

---

## Block Driver

### Initialization

```c
int virtio_blk_init(void);
```

**Process**:
1. Scans for device ID 2 (block device)
2. Performs standard VirtIO handshake
3. Sets up queue 0 with 16 descriptors
4. No feature negotiation (basic operation)
5. Returns 0 on success

### Read/Write Operations

```c
int virtio_blk_rw(uint64_t sector, void *buf, int write);
```

**Parameters**:
- `sector`: 512-byte sector number
- `buf`: 512-byte buffer (must be cache-aligned)
- `write`: 0 for read, 1 for write

**VirtQueue Protocol**:

Uses **3-descriptor chain**:

1. **Descriptor 0**: Request header (`virtio_blk_req`)
   - Type: `VIRTIO_BLK_T_IN` (read) or `VIRTIO_BLK_T_OUT` (write)
   - Sector: LBA sector number
2. **Descriptor 1**: Data buffer (512 bytes)
   - Flags: `WRITE` if reading (device writes), none if writing
3. **Descriptor 2**: Status byte (`virtio_blk_status`)
   - Flags: `WRITE` (device always writes status)

**Cache Coherency**:
- Cleans all descriptors and request header before notify
- For writes: cleans data buffer
- For reads: invalidates data buffer after completion
- Always invalidates status byte after completion

**Timeout**: 5,000,000 iterations (~5 seconds)

### Memory Layout in Queue

```
Offset 0:      Descriptor table (256 bytes for 16 descriptors)
Offset 256:    Available ring
Offset 4096:   Used ring
Offset 5120:   Request structure (static)
Offset 5184:   Status byte (static)
```

**Static buffers** (`blk_qmem + offset`) avoid stack coherency issues with DMA.

---

## Input Driver

### Multi-Device Support

The input driver supports up to **4 simultaneous input devices** (keyboards, mice, tablets).

```c
int virtio_input_init(void);
```

**Process**:
1. Scans all 32 MMIO slots for device ID 18
2. Initializes each found device:
   - Standard VirtIO handshake
   - Queue 0 setup with up to 32 descriptors
   - Pre-populate all descriptors with event buffers
   - Register IRQ handler (IRQ 48 + slot number)
3. Returns 0 if at least one device found

### Event Processing

```c
void virtio_input_poll(void);
void virtio_input_irq_handler(void *arg);
```

**Event Flow**:

1. **IRQ fires** when device has new events
2. **Handler** reads used ring to find completed descriptors
3. **Invalidates** event buffer to see device-written data
4. **Translates** VirtIO events to MYRASPOS input events:
   - `VIRTIO_INPUT_EV_KEY` → `INPUT_TYPE_KEY`
   - `VIRTIO_INPUT_EV_ABS` → `INPUT_TYPE_ABS` (absolute mouse/tablet)
   - `VIRTIO_INPUT_EV_REL` → `INPUT_TYPE_REL` (relative mouse)
5. **Pushes** event to input subsystem via `input_push_event()`
6. **Recycles** descriptor back to available ring
7. **Notifies** device of recycled buffers

### Queue Pre-Population

Unlike GPU and block devices, input queues are **pre-populated**:

```c
for (uint32_t j = 0; j < qsize; j++) {
    desc[j].addr = (uintptr_t)&ev_buf[j];
    desc[j].len = sizeof(struct virtio_input_event);
    desc[j].flags = VIRTQ_DESC_F_WRITE;  // Device writes
    avail->ring[j] = j;
}
avail->idx = qsize;  // All descriptors available
```

This allows the device to write events immediately without waiting for guest requests.

### Event Types

**Keyboard**:
- Type: `VIRTIO_INPUT_EV_KEY`
- Code: Linux keycode (e.g., 30 = 'A')
- Value: 0 (release), 1 (press), 2 (repeat)

**Mouse (Relative)**:
- Type: `VIRTIO_INPUT_EV_REL`
- Code: 0 (X), 1 (Y), 8 (wheel)
- Value: Signed delta

**Mouse (Absolute)**:
- Type: `VIRTIO_INPUT_EV_ABS`
- Code: 0 (X), 1 (Y)
- Value: 0-32767 coordinate

---

## Key Functions

### Public API (virtio.h)

```c
// General initialization (currently stub)
int virtio_init(void);

// GPU functions
int virtio_gpu_init(void);
void virtio_gpu_flush(void);
int virtio_gpu_get_width(void);
int virtio_gpu_get_height(void);

// Block device functions
int virtio_blk_init(void);
int virtio_blk_rw(uint64_t sector, void *buf, int write);

// Input device functions
int virtio_input_init(void);
void virtio_input_poll(void);
```

### Internal Functions

#### Cache Management

```c
void virtio_dcache_clean(void *start, size_t size);
void virtio_dcache_invalidate(void *start, size_t size);
void virtio_flush_dcache(void *start, size_t size);
```

**Purpose**: Ensure cache coherency for DMA operations.

- **Clean (cvac)**: Writeback dirty cache lines so device sees updates
- **Invalidate (ivac)**: Discard cache lines so CPU sees device updates
- **Flush**: Alias for clean (used for consistency)

**ARM64 Operations**:
- `dc cvac`: Clean by VA to PoC (Point of Coherency)
- `dc ivac`: Invalidate by VA to PoC
- `dsb sy`: Data Synchronization Barrier

#### Locking

```c
static unsigned long gpu_acquire_lock(void);
static void gpu_release_lock(unsigned long flags);
```

**Implementation**: ARM64 LDXR/STXR exclusive monitors for spinlock.

```c
static int virtio_gpu_send_command_nolock(void *req, size_t req_len, 
                                          void *resp, size_t resp_len);
```

Unlocked variant for internal use when lock already held.

#### Input Handling

```c
static void virtio_input_handle_dev(struct virtio_input_state *dev);
```

Processes all pending events from a single input device.

---

## Implementation Details

### How VirtQueues Work

#### Descriptor Chains

VirtQueues support **scatter-gather I/O** via descriptor chains:

```
GPU Command (2 descriptors):
[0] Request → [1] Response

Block Read (3 descriptors):
[0] Header → [1] Data (WRITE) → [2] Status (WRITE)

Block Write (3 descriptors):
[0] Header → [1] Data → [2] Status (WRITE)
```

**Chain termination**: Last descriptor has `flags & VIRTQ_DESC_F_NEXT == 0`.

#### Available Ring

Guest-to-host notification:

```c
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;        // Incremented for each new request
    uint16_t ring[qsize]; // Descriptor indices
};
```

**Protocol**:
1. Guest places descriptor chain head index in `ring[idx % qsize]`
2. Memory barrier (`dmb sy`)
3. Increment `idx`
4. Write to QueueNotify register (0x050)

#### Used Ring

Host-to-guest completion:

```c
struct virtq_used {
    uint16_t flags;
    uint16_t idx;        // Incremented for each completion
    struct virtq_used_elem {
        uint32_t id;     // Descriptor chain head
        uint32_t len;    // Bytes written by device
    } ring[qsize];
};
```

**Protocol**:
1. Guest remembers last processed `used.idx`
2. Poll/wait for `used.idx` to advance
3. Process `used.ring[last_idx % qsize]`
4. Increment `last_idx`

### Static Buffer Strategy

All VirtIO drivers use **static, page-aligned buffers**:

```c
static uint8_t queue_mem[8192] __attribute__((aligned(4096)));
static uint8_t gpu_req_static[512] __attribute__((aligned(64)));
static uint8_t gpu_resp_static[512] __attribute__((aligned(64)));
```

**Rationale**:
- **Cache alignment**: Prevents false sharing and torn updates
- **DMA safety**: Physical address = virtual address (identity mapping)
- **Timeout safety**: If device times out, stale pointer won't corrupt stack
- **Coherency**: Easier to manage explicit clean/invalidate

### Synchronization Model

**GPU**: Fully synchronous with spinlock protection
- Commands block until response received
- Lock held across entire transaction
- Timeout returns error without corrupting state

**Block**: Synchronous, no locking (single-threaded during early init)
- Used during boot before scheduler active
- Simple polling for completion

**Input**: Asynchronous with interrupt-driven processing
- IRQ handler reads events and queues them
- Lock protects event queue, not VirtIO operations
- Descriptors recycled after processing

---

## Design Decisions

### Why Synchronous GPU Commands?

**Pros**:
- Simple error handling
- No need for request ID tracking
- Guaranteed ordering

**Cons**:
- Blocks during transfers (acceptable for < 1ms operations)
- Cannot pipeline multiple commands

**Justification**: GPU commands complete in microseconds, async complexity not warranted.

### Why Pre-Populated Input Queues?

Input is **event-driven** by nature:
- Device generates events at unpredictable times
- Guest must have buffers ready to receive
- Pre-population eliminates per-event overhead

### Why No Feature Negotiation?

MYRASPOS uses **baseline VirtIO features only**:
- No indirect descriptors (simple chains sufficient)
- No event suppression (polling/IRQ model simple)
- No notification optimization (not performance-critical)

**Result**: Zero features negotiated = maximum compatibility.

### Memory Layout Choices

**Framebuffer at 0x42000000**:
- 32MB offset ensures no conflict with:
  - Kernel code/data (< 16MB)
  - Boot structures
  - Device tree
- Large enough for 1920x1080x4 = 8.3MB

**Queue Memory Static**:
- Avoids `palloc()` during early boot
- Ensures alignment without runtime computation
- Eliminates risk of fragmentation

### Cache Coherency Strategy

**Explicit management** over hardware coherency:
- ARM64 requires software cache maintenance for DMA
- `dc cvac` (clean) before device reads
- `dc ivac` (invalidate) before CPU reads
- `dsb sy` ensures ordering

**Why not use uncached memory?**
- Performance: uncached access is 10-100× slower
- Complexity: would require separate page table mappings
- Flexibility: cache ops are localized to VirtIO code

---

## Hardware Addresses

### MMIO Register Map (VirtIO 1.0/2.0)

Base addresses: **0x0A000000 + (slot × 0x200)**, slots 0-31

| Offset | Register                | Access | Description |
|--------|-------------------------|--------|-------------|
| 0x000  | MagicValue              | R      | 0x74726976 ("virt") |
| 0x004  | Version                 | R      | 1 (legacy) or 2 (modern) |
| 0x008  | DeviceID                | R      | Device type (2/16/18) |
| 0x00C  | VendorID                | R      | 0x554D4551 ("QEMU") |
| 0x010  | DeviceFeatures          | R      | Feature bits (low 32) |
| 0x014  | DeviceFeaturesSel       | W      | Select feature bits [63:32] |
| 0x020  | DriverFeatures          | W      | Accepted features (low 32) |
| 0x024  | DriverFeaturesSel       | W      | Select driver features [63:32] |
| 0x028  | GuestPageSize           | W      | Page size (legacy only) |
| 0x030  | QueueSel                | W      | Select queue number |
| 0x034  | QueueNumMax             | R      | Max queue size |
| 0x038  | QueueNum                | W      | Actual queue size |
| 0x03C  | QueueAlign              | W      | Alignment (legacy only) |
| 0x040  | QueuePFN                | RW     | Queue page number (legacy) |
| 0x044  | QueueReady              | RW     | Queue enabled (modern) |
| 0x050  | QueueNotify             | W      | Notify queue (write queue #) |
| 0x060  | InterruptStatus         | R      | IRQ status bits |
| 0x064  | InterruptACK            | W      | Clear IRQ bits |
| 0x070  | Status                  | RW     | Device status |
| 0x080  | QueueDescLow            | W      | Desc table address [31:0] |
| 0x084  | QueueDescHigh           | W      | Desc table address [63:32] |
| 0x090  | QueueAvailLow           | W      | Avail ring address [31:0] |
| 0x094  | QueueAvailHigh          | W      | Avail ring address [63:32] |
| 0x0A0  | QueueUsedLow            | W      | Used ring address [31:0] |
| 0x0A4  | QueueUsedHigh           | W      | Used ring address [63:32] |

### Status Register Bits

| Bit | Name         | Description |
|-----|--------------|-------------|
| 0   | ACKNOWLEDGE  | Guest detected device |
| 1   | DRIVER       | Guest has driver |
| 2   | DRIVER_OK    | Driver ready |
| 3   | FEATURES_OK  | Feature negotiation complete |
| 6   | DEVICE_NEEDS_RESET | Device error |
| 7   | FAILED       | Fatal error |

### Interrupt Status Bits

| Bit | Name         | Description |
|-----|--------------|-------------|
| 0   | VRING        | Used buffer notification |
| 1   | CONFIG       | Configuration change |

### Legacy vs Modern MMIO

**Legacy (Version 1)**:
- Uses `QueuePFN` (0x040) for single base address
- Requires `GuestPageSize` (0x028) and `QueueAlign` (0x03C)
- Queue layout calculated from base

**Modern (Version 2)**:
- Uses separate `QueueDesc`, `QueueAvail`, `QueueUsed` registers
- More flexible layout
- Requires `FEATURES_OK` handshake

MYRASPOS **supports both** for maximum compatibility.

---

## Usage Examples

### Example 1: Initialize and Use GPU

```c
#include "virtio.h"
#include "framebuffer.h"

void setup_graphics(void) {
    // Initialize VirtIO GPU
    if (virtio_gpu_init() < 0) {
        uart_puts("GPU init failed\n");
        return;
    }
    
    // Get resolution
    int width = virtio_gpu_get_width();
    int height = virtio_gpu_get_height();
    
    // Initialize framebuffer at GPU backing memory
    fb_init((uint32_t *)0x42000000, width, height, width);
    
    // Draw something
    fb_rect(10, 10, 100, 100, 0xFF0000FF);  // Red rectangle
    
    // Flush to display
    virtio_gpu_flush();
}
```

**From `kernel/framebuffer.c`**:

```c
void fb_flip(void) {
    // ... copy backbuffer to front ...
    virtio_gpu_flush();  // Update display
}
```

### Example 2: Read Disk Sector (diskfs.c)

```c
#include "virtio.h"

void diskfs_init(void) {
    // Initialize block device
    if (virtio_blk_init() < 0) {
        uart_puts("No disk found\n");
        return;
    }
    
    // Read directory sectors
    for (int i = 0; i < NUM_DIR_SECTORS; i++) {
        uint8_t *buf = dir_cache + i * SECTOR_SIZE;
        if (virtio_blk_rw(DIR_START_SECTOR + i, buf, 0) < 0) {
            uart_puts("Read failed\n");
        }
    }
}
```

### Example 3: Write Disk Sector (diskfs.c)

```c
void diskfs_write_file(const char *path, const void *data, size_t size) {
    // ... find file entry ...
    
    uint32_t sector = file->first_sector;
    const uint8_t *src = (const uint8_t *)data;
    
    while (size > 0) {
        if (size >= 512) {
            // Full sector write
            virtio_blk_rw(sector, (void *)src, 1);
        } else {
            // Partial sector: read-modify-write
            uint8_t sector_buf[512];
            virtio_blk_rw(sector, sector_buf, 0);   // Read
            memcpy(sector_buf, src, size);
            virtio_blk_rw(sector, sector_buf, 1);   // Write
        }
        src += 512;
        size -= (size >= 512) ? 512 : size;
        sector++;
    }
}
```

### Example 4: Initialize Input (main.c)

```c
#include "virtio.h"
#include "input.h"

void kernel_init(void) {
    // ... other init ...
    
    // Initialize VirtIO input devices
    if (virtio_input_init() < 0) {
        uart_puts("No input devices found\n");
    }
    
    // Set up input subsystem
    input_init(virtio_gpu_get_width(), virtio_gpu_get_height());
}
```

### Example 5: Process Input Events (scheduler)

```c
// Called from timer interrupt or idle loop
void scheduler_tick(void) {
    // Poll all input devices
    virtio_input_poll();
    
    // Input events are now in input subsystem queues
    // Window manager can retrieve them via input_get_*()
}
```

### Example 6: Custom GPU Command

```c
// Internal use only - demonstrates command protocol
static void custom_gpu_operation(void) {
    unsigned long flags = gpu_acquire_lock();
    
    // Build command
    struct virtio_gpu_ctrl_hdr cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VIRTIO_GPU_CMD_CUSTOM;
    
    // Send and wait for response
    struct virtio_gpu_ctrl_hdr resp;
    int ret = virtio_gpu_send_command_nolock(&cmd, sizeof(cmd), 
                                             &resp, sizeof(resp));
    
    if (ret < 0 || resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_puts("Command failed\n");
    }
    
    gpu_release_lock(flags);
}
```

---

## Cross-References

### Related Files

- **[framebuffer.c](framebuffer.c.md)**: Uses `virtio_gpu_flush()` to update display
- **[diskfs.c](../fs/diskfs.c.md)**: Uses `virtio_blk_rw()` for disk I/O
- **[input.c](../io/input.c.md)**: Receives events from `virtio_input_poll()`
- **[irq.c](../core/irq.c.md)**: Dispatches VirtIO input IRQs
- **[palloc.c](../memory/palloc.c.md)**: Physical memory allocator (not used by VirtIO)
- **[kernel.c](../core/kernel.c.md)**: Calls initialization functions

### Related Documentation

- **[VirtIO Specification 1.0](https://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html)**
- **[VirtIO MMIO Transport](https://docs.oasis-open.org/virtio/virtio/v1.0/cs04/virtio-v1.0-cs04.html#x1-1440002)**
- **ARM Architecture Reference Manual**: Cache maintenance operations
- **QEMU VirtIO Documentation**: Device-specific protocols

### VirtIO Specification References

- **Section 2.4**: Virtqueues
- **Section 4.1**: Virtio Over MMIO
- **Section 5.7**: GPU Device
- **Section 5.2**: Block Device
- **Section 5.8**: Input Device

### Design Patterns

- **Command Pattern**: GPU commands as request/response structures
- **Ring Buffer**: Available/Used rings for async communication
- **Pre-allocation**: Static buffers for DMA safety
- **Scatter-Gather**: Descriptor chains for complex I/O

---

## Debugging Tips

### Common Issues

**GPU timeout**:
- Check that framebuffer is at 0x42000000
- Verify cache is cleaned before notify
- Ensure QueueNotify is written correctly

**Block I/O corruption**:
- Invalidate buffer after reads
- Clean buffer before writes
- Check descriptor flags (WRITE for device-writable)

**No input events**:
- Verify IRQ handler is registered
- Check that descriptors have WRITE flag
- Ensure avail ring is pre-populated

### Debug Logging

Enable verbose output:

```c
uart_puts("[virtio] type="); uart_put_hex(type);
uart_puts(" code="); uart_put_hex(code);
uart_puts(" val="); uart_put_hex(value);
uart_puts("\n");
```

Located in `virtio_input_handle_dev()` around line 862.

### Cache Coherency Verification

Add explicit flushes:

```c
virtio_flush_dcache(qmem, 8192);  // Flush entire queue
__asm__ volatile("dsb sy" ::: "memory");  // Ensure completion
```

### Register Dumps

```c
volatile uint32_t *r = (volatile uint32_t *)mmio_base;
uart_puts("Magic="); uart_put_hex(r[0]);
uart_puts(" Version="); uart_put_hex(r[1]);
uart_puts(" DevID="); uart_put_hex(r[2]);
uart_puts(" Status="); uart_put_hex(r[0x070/4]);
uart_puts("\n");
```

---

## Performance Characteristics

| Operation | Typical Latency | Notes |
|-----------|-----------------|-------|
| GPU Flush | 50-200 μs | Includes transfer + flush |
| Block Read | 100-500 μs | 512 bytes |
| Block Write | 100-500 μs | 512 bytes |
| Input Event | < 10 μs | Interrupt to queue |

**GPU Bandwidth**: ~50 MB/s (framebuffer transfers)  
**Block Bandwidth**: ~10 MB/s (single queue, synchronous)  
**Input Latency**: < 1ms (IRQ to application)

### Optimization Opportunities

1. **GPU**: Implement dirty rectangle tracking to reduce transfer size
2. **Block**: Use larger transfers (multi-sector I/O)
3. **Input**: Batch event processing to reduce IRQ overhead
4. **General**: Implement indirect descriptors for complex chains

---

## Future Enhancements

### Planned Features

- **VirtIO-Net**: Network device support
- **VirtIO-RNG**: Hardware random number generator
- **VirtIO-Serial**: Console/serial ports
- **Multi-queue**: Parallel I/O for block/network

### Async GPU

Convert to async submission with completion callbacks:

```c
int virtio_gpu_flush_async(void (*callback)(void *), void *arg);
```

Would enable overlapping CPU work with GPU updates.

### DMA-BUF Integration

Share GPU resources with other devices (future camera, video):

```c
int virtio_gpu_export_resource(uint32_t res_id, int *dma_buf_fd);
```

### Feature Negotiation

Enable advanced features:
- Event index suppression (reduce IRQ rate)
- Indirect descriptors (complex I/O)
- Packed virtqueues (VirtIO 1.1)

---

**Last Updated**: 2024-02-06  
**Author**: MYRASPOS Kernel Team  
**Status**: Production / Stable
