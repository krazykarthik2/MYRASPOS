.section ".text.boot"
.global _start

_start:
    // park secondary cores FIRST
    mrs x10, mpidr_el1
    and x10, x10, #3
    cbz x10, primary_core

secondary_hang:
    wfe
    b secondary_hang

primary_core:
    // --- MINI UART INIT ---
    // Load peripheral base 0x3F000000 safely
    movz x0, #0x0000
    movk x0, #0x3F00, lsl #16

    // Enable AUX (AUX_ENABLES = 0x3F215004)
    movz x1, #0x5004
    movk x1, #0x21, lsl #16
    add x1, x0, x1
    mov w2, #1
    str w2, [x1]

    // Disable TX/RX while configuring (AUX_MU_CNTL = 0x3F215060)
    movz x1, #0x5060
    movk x1, #0x21, lsl #16
    add x1, x0, x1
    mov w2, #0
    str w2, [x1]

    // Set 8-bit mode (AUX_MU_LCR = 0x3F21504C)
    movz x1, #0x504C
    movk x1, #0x21, lsl #16
    add x1, x0, x1
    mov w2, #3
    str w2, [x1]

    // Set baud (AUX_MU_BAUD = 0x3F215068) to 270 (~115200 baud)
    movz x1, #0x5068
    movk x1, #0x21, lsl #16
    add x1, x0, x1
    mov w2, #270
    str w2, [x1]

    // --- CONFIGURE GPIO14/15 TO ALT5 ---
    // GPFSEL1 = 0x3F200004
    movz x1, #0x0004
    movk x1, #0x3F20, lsl #16
    ldr w2, [x1]
    
    // clear bits 12-14 (GPIO14) and 15-17 (GPIO15)
    mov w3, #(7 << 12)
    bic w2, w2, w3
    mov w3, #(7 << 15)
    bic w2, w2, w3
    
    // set ALT5 (010) for both
    mov w3, #(2 << 12)
    orr w2, w2, w3
    mov w3, #(2 << 15)
    orr w2, w2, w3
    str w2, [x1]

    // --- ENABLE TX/RX ---
    movz x1, #0x5060
    movk x1, #0x21, lsl #16
    add x1, x0, x1
    mov w2, #3
    str w2, [x1]

    // --- TEST UART IMMEDIATELY ---
    mov w0, #'H'
    bl uart_send
    mov w0, #'i'
    bl uart_send

    // --- MAILBOX SETUP (.bss) ---
    // We populate the mailbox buffer at runtime
    ldr x0, =mailbox
    
    // Header
    mov w1, #140           // message size (35 words * 4 bytes)
    str w1, [x0, #0]
    mov w1, #0             // request code
    str w1, [x0, #4]
    
    // Tag: Set Physical Size (0x48003, 8 bytes, 8 bytes, 1024, 768)
    mov w1, #0x8003
    movk w1, #0x0004, lsl #16
    str w1, [x0, #8]       // Tag ID
    mov w1, #8
    str w1, [x0, #12]      // Tag buffer size
    str w1, [x0, #16]      // Tag request/response code
    mov w1, #1024
    str w1, [x0, #20]      // Width
    mov w1, #768
    str w1, [x0, #24]      // Height
    
    // Tag: Set Virtual Size (0x48004, 8 bytes, 8 bytes, 1024, 768)
    mov w1, #0x8004
    movk w1, #0x0004, lsl #16
    str w1, [x0, #28]
    mov w1, #8
    str w1, [x0, #32]
    str w1, [x0, #36]
    mov w1, #1024
    str w1, [x0, #40]
    mov w1, #768
    str w1, [x0, #44]
    
    // Tag: Set Depth (0x48005, 4 bytes, 4 bytes, 32 bit)
    mov w1, #0x8005
    movk w1, #0x0004, lsl #16
    str w1, [x0, #48]
    mov w1, #4
    str w1, [x0, #52]
    str w1, [x0, #56]
    mov w1, #32
    str w1, [x0, #60]
    
    // Tag: Allocate Framebuffer (0x40001, 8 bytes, 8 bytes, 16 align, 0 ptr)
    mov w1, #0x0001
    movk w1, #0x0004, lsl #16
    str w1, [x0, #64]
    mov w1, #8
    str w1, [x0, #68]
    str w1, [x0, #72]
    mov w1, #16
    str w1, [x0, #76]      // Alignment
    mov w1, #0
    str w1, [x0, #80]      // Value 2 (Pointer output will go here)
    str w1, [x0, #84]      // Value 3 (Size output will go here)
    
    // End tag
    str w1, [x0, #88]      // 0 terminator

    // --- CALL MAILBOX ---
    ldr x0, =mailbox
    bl mailbox_call

    // --- VERIFY SUCCESS ---
    ldr x0, =mailbox
    ldr w2, [x0, #4]
    movz w3, #0x0000
    movk w3, #0x8000, lsl #16   // 0x80000000
    cmp w2, w3
    b.ne error_hang

    // --- GET FRAMEBUFFER POINTER ---
    // In our structure, Pointer is at offset 80 (Word 20)
    ldr w1, [x0, #80]
    // MASK: Convert GPU bus address to ARM physical address
    and w1, w1, #0x3FFFFFFF

    // --- PAINT SCREEN WHITE ---
    mov w2, #0xFFFFFFFF   // white
    mov x3, #(1024*768)

paint_loop:
    str w2, [x1], #4
    subs x3, x3, #1
    b.ne paint_loop

hang:
    wfe
    b hang

error_hang:
    wfe
    b error_hang

// --- FUNCTIONS ---

uart_send:
    // Utility: Wait for UART to be ready and send character in w0
    movz x1, #0x0000
    movk x1, #0x3F00, lsl #16
    movz x2, #0x5054       // AUX_MU_LSR_REG
    movk x2, #0x21, lsl #16
    add x2, x1, x2
wait_tx:
    ldr w3, [x2]
    tst w3, #0x20           // bit 5: Transmit FIFO can accept at least one byte
    b.eq wait_tx            // Wait while it is NOT ready (bit 5 == 0)
    
    movz x2, #0x5040       // AUX_MU_IO_REG
    movk x2, #0x21, lsl #16
    add x2, x1, x2
    str w0, [x2]
    ret

mailbox_call:
    // Utility: Send message at address x0 via mailbox channel 8
    movz x1, #0xB880
    movk x1, #0x3F00, lsl #16   // mailbox base
wait_full:
    ldr w2, [x1, #0x18]     // status register
    tst w2, #0x80000000     // full bit
    b.ne wait_full

    // write message address | channel 8
    orr x0, x0, #8
    str w0, [x1, #0x20]     // write register

wait_resp:
    ldr w2, [x1, #0x18]
    tst w2, #0x40000000     // empty bit
    b.ne wait_resp          // if bit is NOT set (NE), it's not empty? No, TST/BNE means branch if set.
    // wait_resp logic: 
    // ldr w2, [x1, #0x18]
    // tst w2, #0x40000000
    // b.ne wait_resp  <- This branches if the EMPTY bit is SET (meaning it is empty).
    // Correct logic for mailbox wait_resp usually polls until the bit is 0 (not empty).
    // Actually, bit 30 (0x40000000) is the EMPTY bit. 0 = not empty, 1 = empty.
    // So if TST sets NE, it IS empty. We want to wait WHILE empty. So BNE is correct.

    ldr w2, [x1]            // read register
    cmp w2, w0              // check channel match
    b.ne wait_resp
    ret

.section ".bss"
.balign 16
mailbox:
    .skip 256
