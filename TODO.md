# OS TODO List

---

## 🔥 CURRENT: Kernel Scheduler Crash Fix

**Status**: Testing with proper stack sizes

### Completed
- ✅ Fixed race condition in `draw_taskbar()` (missing window list locking)
- ✅ Added interrupt masking around `cpu_switch_to()` 
- ✅ Fixed debug instrumentation in `swtch.S` (register corruption)
- ✅ Implemented stack guards: 4KB guard page + canary + SP validation
- ✅ Added per-task stack sizing (`task_create_with_stack`)
- ✅ Init task: 64KB stack (needed for virtio + service bootup)
- ✅ Regular tasks: 16KB stack (default)

### Testing
- ⏳ Verifying `myra_app` creation completes without stack overflow

---

## Kernel core

- add wait queue struct
- add sleep with timeout
- wake task from IRQ context
- ~~add per-task ID~~ ✅ (already implemented)
- add kernel ring buffer logger
- ~~add panic stack trace~~ ✅ (basic backtrace exists)
---

## Syscalls / blocking

- make `read()` block correctly
- make `write()` non-blocking option
- add `poll()` syscall
- add fd readiness flags

---

## Device framework

- create `struct device`
- create `struct driver`
- implement device registration
- add `/dev` filesystem entries

---

## GPIO

- init GPIO controller
- configure pin direction
- add edge interrupt support
- block on GPIO read
- wake task on GPIO IRQ

---

## SPI

- init SPI controller
- configure clock polarity/phase
- implement blocking transfer
- expose `/dev/spi0`

---

## I²C

- init I²C controller
- implement START/STOP
- implement read/write transaction
- handle NACK errors
- expose `/dev/i2c1`

---

## SDIO (Wi-Fi)

- init SDIO host
- set SDIO clock
- implement CMD52
- implement CMD53
- enable SDIO IRQ
- map SDIO interrupt handler

---

## Wi-Fi driver (Pi Zero 2 W)

- upload Broadcom firmware
- wait for firmware ready
- send raw TX frame
- receive raw RX frame
- implement RX ring buffer
- register `wlan0` net_device

---

## Network core

- define packet buffer struct (skb)
- allocate/free skb
- implement netdev RX entry point
- implement netdev TX entry point

---

## Ethernet / ARP

- parse ethernet header
- dispatch ethertype
- implement ARP request
- implement ARP reply
- add ARP cache

---

## IPv4

- parse IP header
- validate checksum
- drop unsupported packets
- route packet to protocol handler

---

## UDP

- create UDP socket type
- bind UDP socket
- send UDP packet
- receive UDP packet
- block on `recvfrom()`

---

## Socket layer

- implement `socket()`
- implement `bind()`
- implement `sendto()`
- implement `recvfrom()`
- integrate sockets with `poll()`

---

## init / services

- implement PID 1
- parse service config
- start netd
- restart crashed service

---

## netd (user space)

- bring up wlan0
- assign static IP
- add default route
- expose control socket

---

## TCP

- define tcp_sock struct
- implement TCP state machine
- handle SYN
- handle SYN-ACK
- handle ACK
- implement send buffer
- implement receive buffer
- implement retransmission timer
- handle FIN
- implement `accept()`
- implement `connect()`
- block on TCP recv

---

## HTTP (user space)

- open TCP listen socket
- parse HTTP request line
- return static response
- add `/status` endpoint

---

## HTTPS (bonus)

- integrate TLS library
- perform TLS handshake
- wrap TCP socket with TLS
- serve HTTPS response

---

## Memory Management
0.
- ✅ Implement virtual memory and paging
- ✅ identity mapping for kernel and peripherals
- ✅ page table setup (L0-L3)
- ✅ enable MMU

---

## Disk Filesystem (DiskFS)

- ✅ Implement virtio-blk driver
- ✅ Create simple diskfs
- ✅ Move all existing files to diskfs
- ✅ provide persistence across reboots (simulated via virtio-blk)
