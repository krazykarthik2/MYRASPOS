@echo on
set REAL_FLAG=-DREAL
set LINKER_SCRIPT=linkers/linker_pi.ld
set GCC="aarch64/aarch64-none-elf-gcc.bat"
set C_FLAGS=-ffixed-x18 -fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes -Ikernel -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK %REAL_FLAG%

echo Compiling uart...
call %GCC% %C_FLAGS% -c kernel/uart.c -o temp/objects/uart.o
echo Linking...
call "aarch64/aarch64-none-elf-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/rpi_fx.o temp/objects/uart.o temp/objects/framebuffer.o temp/objects/lib.o temp/objects/sched.o temp/objects/kmalloc.o temp/objects/palloc.o temp/objects/mmu.o temp/objects/irq.o temp/objects/timer.o temp/objects/vectors.o temp/objects/swtch.o -T %LINKER_SCRIPT% -o temp/elfs/kernel.elf
