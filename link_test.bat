@echo off
"aarch64/aarch64-none-elf-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/rpi_fx.o temp/objects/uart.o temp/objects/framebuffer.o temp/objects/lib.o temp/objects/sched.o temp/objects/kmalloc.o temp/objects/palloc.o temp/objects/mmu.o temp/objects/irq.o temp/objects/timer.o temp/objects/vectors.o temp/objects/swtch.o -T linkers/linker_pi.ld -o temp/elfs/kernel.elf
