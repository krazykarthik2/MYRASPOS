
@echo ON
del /F /Q temp\objects\*.o
del /F /Q temp\elfs\*.elf
del /F /Q temp\binaries\*.img
del /F /Q temp\maps\*.map



call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c boot/start.S -o temp/objects/start.o 
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/kernel.c -o temp/objects/kernel.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/uart.c -o temp/objects/uart.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/palloc.c -o temp/objects/palloc.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/kmalloc.c -o temp/objects/kmalloc.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/ramfs.c -o temp/objects/ramfs.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/lib.c -o temp/objects/lib.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/syscall.c -o temp/objects/syscall.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/timer.c -o temp/objects/timer.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/irq.c -o temp/objects/irq.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/framebuffer.c -o temp/objects/framebuffer.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/virtio.c -o temp/objects/virtio.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/init.c -o temp/objects/init.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/programs.c -o temp/objects/programs.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/echo.c -o temp/objects/echo.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/help.c -o temp/objects/help.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/touch.c -o temp/objects/touch.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/write.c -o temp/objects/write.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/cat.c -o temp/objects/cat.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/ls.c -o temp/objects/ls.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/rm.c -o temp/objects/rm.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/mkdir.c -o temp/objects/mkdir.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/rmdir.c -o temp/objects/rmdir.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/cp.c -o temp/objects/cp.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/mv.c -o temp/objects/mv.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/grep.c -o temp/objects/grep.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/head.c -o temp/objects/head.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/tail.c -o temp/objects/tail.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/more.c -o temp/objects/more.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/tree.c -o temp/objects/tree.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/shell.c -o temp/objects/shell.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/sched.c -o temp/objects/sched.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/panic.c -o temp/objects/panic.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/service.c -o temp/objects/service.o
call "aarch64/aarch64-none-elf-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra  -Wmissing-prototypes -c kernel/glob.c -o temp/objects/glob.o


call "aarch64/aarch64-none-elf-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/uart.o temp/objects/palloc.o temp/objects/kmalloc.o temp/objects/ramfs.o temp/objects/lib.o temp/objects/syscall.o temp/objects/timer.o temp/objects/irq.o temp/objects/framebuffer.o temp/objects/virtio.o temp/objects/init.o temp/objects/programs.o temp/objects/echo.o temp/objects/help.o temp/objects/touch.o temp/objects/write.o temp/objects/cat.o temp/objects/ls.o temp/objects/rm.o temp/objects/mkdir.o temp/objects/rmdir.o temp/objects/cp.o temp/objects/mv.o temp/objects/grep.o temp/objects/head.o temp/objects/tail.o temp/objects/more.o temp/objects/tree.o temp/objects/shell.o temp/objects/sched.o temp/objects/panic.o temp/objects/service.o temp/objects/glob.o -T linkers/linker.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map
@REM call "arm-none/arm-none-eabi-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/uart.o -T linkers/linker_pi.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map 
@REM call "arm-none/arm-none-eabi-objcopy.bat" -O binary -S temp/elfs/kernel.elf temp/binaries/kernel.img
call "aarch64/aarch64-none-elf-objcopy.bat" -O binary temp/elfs/kernel.elf temp/binaries/kernel8.img
@REM call "qemu/qemu-system-aarch64.bat" -machine virt -cpu cortex-15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio
echo Launching QEMU with SDL + ramfb (fallback) and virtio-gpu
@REM call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -display sdl -device virtio-gpu-pci -device ramfb -kernel temp/elfs/kernel.elf -serial mon:stdio
call "qemu/qemu-system-aarch64.bat" ^
  -machine virt ^
  -cpu cortex-a53 ^
  -display sdl ^
   -device virtio-gpu-device ^
   -device ramfb ^
  -kernel temp/elfs/kernel.elf ^
  -serial mon:stdio
@REM call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio