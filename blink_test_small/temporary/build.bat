@echo ON

@REM Always recreate disk.img to ensure assets are fresh==

set GCC="../../aarch64/aarch64-none-elf-gcc.bat"
set C_FLAGS= -ffixed-x18 -fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes -Ikernel -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK %REAL_FLAG%

call %GCC% %C_FLAGS% -c boot.S -o boot.o
call %GCC% %C_FLAGS% -c kernel.c -o kernel.o

set LINKER_SCRIPT="linker.ld"
call "../../aarch64/aarch64-none-elf-ld.bat" boot.o kernel.o -T %LINKER_SCRIPT% -o kernel.elf -Map kernel.map
call "../../aarch64/aarch64-none-elf-objcopy.bat" -O binary kernel.elf kernel8.img
