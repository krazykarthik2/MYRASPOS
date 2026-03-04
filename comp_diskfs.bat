@echo OFF
aarch64\aarch64-none-elf-gcc.bat -ffixed-x18 -fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes -Ikernel -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK -c kernel\diskfs.c -o temp\objects\diskfs.o
