@echo off
echo Compiling virtio.c...
call aarch64\aarch64-none-elf-gcc.bat -ffixed-x18 -fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes -Ikernel -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK -c kernel\virtio.c -o temp\objects\virtio.o > virtio_compile.log 2>&1
echo Done.
type virtio_compile.log
