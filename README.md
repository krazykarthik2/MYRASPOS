# 🐧 MYRASPOS
### **A Minimalist AArch64 Micro-Kernel**

`MYRASPOS` is a boutique kernel crafted for the **ARM (AArch64)** architecture. It serves as a lean, low-level playground for exploring preemptive multitasking, hardware abstraction, and kernel-space memory management on the Raspberry Pi.

---

## ⚡ Core Architecture

The system operates on a **Circular Task Ring**, ensuring that the CPU never stops moving.

* **Round-Robin Scheduler:** A balanced execution engine that cycles through active tasks.
* **State Persistence:** High-fidelity context switching via `cpu_switch_to`, preserving the exact CPU state including `sp` and `x19-x30` registers.
* **Lifecycle Management:** Advanced task tracking featuring parent-child hierarchies and recursive cleanup.
* **Safety Guardrails:** Real-time stack corruption detection using `0xDEADC0DE` magic numbers and pointer alignment validation.



---

## 🏗️ Technical Stack

* **Kernel:** Preemptive multitasking core.
* **Memory:** Integrated `palloc` (page) and `kmalloc` (heap) allocators.
* **I/O:** Interrupt-driven UART and system timer integration.
* **Debugging:** Granular UART tracing for task transitions and IRQ dispatching.



---

## 🕹️ Emulation

To launch the kernel in a virtualized Raspberry Pi 3 environment, use the provided simulation script.

### `sim.bat`
