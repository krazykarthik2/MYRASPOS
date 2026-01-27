A. Cursor & Input Responsiveness (UI / UX)

Cursor as overlay layer

Cursor must NOT be part of WM redraw

Drawn last, top-most

Independent of window rendering

Dedicated high-priority cursor task

Interrupt-driven mouse input

IRQ updates mouse state only

Cursor task restores old area + redraws cursor

Highest priority, minimal work

B. System-Wide Performance Improvements (1–9 locked)

Event-driven architecture

No busy loops

WM, shell, apps wake only on events

No unnecessary redraws

Redraw only when state changes

No periodic full redraws

Dirty rectangles / damage regions

Track changed screen regions

Partial framebuffer blits only

Scheduler optimizations

Avoid context switch when only one runnable task

Batch wakeups

Reduce timer tick frequency

Minimal IRQ work

IRQ handlers only:

capture data

set flags

wake tasks

No rendering / allocation / FS in IRQ

Allocator hot-path fixes

No kmalloc/free in:

render paths

input paths

Introduce object pools where needed

Filesystem caching

Path lookup cache

Directory entry cache

Avoid repeated disk walks

Remove logging from hot paths

No printf in:

scheduler

IRQs

render loop

Compile-time debug flags only

Graphics & text optimizations

Cache glyphs

Redraw text only on content change

No text redraw per frame

(Explicitly excluding profiling/visibility tools — per your instruction)

C. Editor (Userland / Kernel App)

Simple Vim-style text editor

Modal (normal / insert)

Cursor movement

Insert / delete

File load / save

Basic viewport scrolling

Naive implementation (no undo tree, no plugins)

D. Networking Stack (Strict Scope)

WiFi-only networking

Target SoC: Raspberry Pi Zero 2W

No Ethernet driver

QEMU virtio-net for development

Networking correctness first

Hardware later

TCP/IP stack from scratch

ARP

IPv4

TCP (blocking, minimal)

No congestion control

Limited concurrent connections

HTTP implementation

HTTP server

HTTP client

HTTP/1.0 or minimal 1.1

No HTTPS (explicitly excluded)

WiFi integration (final phase)

Use Pi firmware blobs

Minimal SDIO bring-up

Treat WiFi as packet transport only