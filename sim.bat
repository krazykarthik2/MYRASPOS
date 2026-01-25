@echo ON
if not exist disk.img (
    echo Creating disk.img...
    python make_disk.py
)
del /F /Q temp\objects\*.o
del /F /Q temp\elfs\*.elf
del /F /Q temp\binaries\*.img
del /F /Q temp\maps\*.map

set GCC="aarch64/aarch64-none-elf-gcc.bat"
set C_FLAGS=-fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes

call %GCC% %C_FLAGS% -c boot/start.S -o temp/objects/start.o 
call %GCC% %C_FLAGS% -c kernel/vectors.S -o temp/objects/vectors.o
call %GCC% %C_FLAGS% -c kernel/swtch.S -o temp/objects/swtch.o
call %GCC% %C_FLAGS% -c kernel/kernel.c -o temp/objects/kernel.o
call %GCC% %C_FLAGS% -c kernel/uart.c -o temp/objects/uart.o
call %GCC% %C_FLAGS% -c kernel/palloc.c -o temp/objects/palloc.o
call %GCC% %C_FLAGS% -c kernel/kmalloc.c -o temp/objects/kmalloc.o
call %GCC% %C_FLAGS% -c kernel/ramfs.c -o temp/objects/ramfs.o
call %GCC% %C_FLAGS% -c kernel/lib.c -o temp/objects/lib.o
call %GCC% %C_FLAGS% -c kernel/syscall.c -o temp/objects/syscall.o
call %GCC% %C_FLAGS% -c kernel/timer.c -o temp/objects/timer.o
call %GCC% %C_FLAGS% -c kernel/irq.c -o temp/objects/irq.o
call %GCC% %C_FLAGS% -c kernel/framebuffer.c -o temp/objects/framebuffer.o
call %GCC% %C_FLAGS% -c kernel/virtio.c -o temp/objects/virtio.o
call %GCC% %C_FLAGS% -c kernel/mmu.c -o temp/objects/mmu.o
call %GCC% %C_FLAGS% -c kernel/diskfs.c -o temp/objects/diskfs.o
call %GCC% %C_FLAGS% -c kernel/init.c -o temp/objects/init.o
call %GCC% %C_FLAGS% -c kernel/programs.c -o temp/objects/programs.o
call %GCC% %C_FLAGS% -c kernel/echo.c -o temp/objects/echo.o
call %GCC% %C_FLAGS% -c kernel/help.c -o temp/objects/help.o
call %GCC% %C_FLAGS% -c kernel/touch.c -o temp/objects/touch.o
call %GCC% %C_FLAGS% -c kernel/write.c -o temp/objects/write.o
call %GCC% %C_FLAGS% -c kernel/cat.c -o temp/objects/cat.o
call %GCC% %C_FLAGS% -c kernel/ls.c -o temp/objects/ls.o
call %GCC% %C_FLAGS% -c kernel/rm.c -o temp/objects/rm.o
call %GCC% %C_FLAGS% -c kernel/mkdir.c -o temp/objects/mkdir.o
call %GCC% %C_FLAGS% -c kernel/rmdir.c -o temp/objects/rmdir.o
call %GCC% %C_FLAGS% -c kernel/cp.c -o temp/objects/cp.o
call %GCC% %C_FLAGS% -c kernel/mv.c -o temp/objects/mv.o
call %GCC% %C_FLAGS% -c kernel/grep.c -o temp/objects/grep.o
call %GCC% %C_FLAGS% -c kernel/head.c -o temp/objects/head.o
call %GCC% %C_FLAGS% -c kernel/tail.c -o temp/objects/tail.o
call %GCC% %C_FLAGS% -c kernel/more.c -o temp/objects/more.o
call %GCC% %C_FLAGS% -c kernel/tree.c -o temp/objects/tree.o
call %GCC% %C_FLAGS% -c kernel/shell.c -o temp/objects/shell.o
call %GCC% %C_FLAGS% -c kernel/sched.c -o temp/objects/sched.o
call %GCC% %C_FLAGS% -c kernel/panic.c -o temp/objects/panic.o
call %GCC% %C_FLAGS% -c kernel/service.c -o temp/objects/service.o
call %GCC% %C_FLAGS% -c kernel/glob.c -o temp/objects/glob.o
call %GCC% %C_FLAGS% -c kernel/pty.c -o temp/objects/pty.o
call %GCC% %C_FLAGS% -c kernel/input.c -o temp/objects/input.o
call %GCC% %C_FLAGS% -c kernel/wm.c -o temp/objects/wm.o
call %GCC% %C_FLAGS% -c kernel/terminal_app.c -o temp/objects/terminal_app.o
call %GCC% %C_FLAGS% -c kernel/myra_app.c -o temp/objects/myra_app.o
call %GCC% %C_FLAGS% -c kernel/calculator_app.c -o temp/objects/calculator_app.o
call %GCC% %C_FLAGS% -c kernel/files_app.c -o temp/objects/files_app.o


call "aarch64/aarch64-none-elf-ld.bat" temp/objects/start.o temp/objects/mmu.o temp/objects/diskfs.o temp/objects/vectors.o temp/objects/swtch.o temp/objects/kernel.o temp/objects/uart.o temp/objects/palloc.o temp/objects/kmalloc.o temp/objects/ramfs.o temp/objects/lib.o temp/objects/syscall.o temp/objects/timer.o temp/objects/irq.o temp/objects/framebuffer.o temp/objects/virtio.o temp/objects/init.o temp/objects/programs.o temp/objects/echo.o temp/objects/help.o temp/objects/touch.o temp/objects/write.o temp/objects/cat.o temp/objects/ls.o temp/objects/rm.o temp/objects/mkdir.o temp/objects/rmdir.o temp/objects/cp.o temp/objects/mv.o temp/objects/grep.o temp/objects/head.o temp/objects/tail.o temp/objects/more.o temp/objects/tree.o temp/objects/shell.o temp/objects/sched.o temp/objects/panic.o temp/objects/service.o temp/objects/glob.o temp/objects/pty.o temp/objects/input.o temp/objects/wm.o temp/objects/terminal_app.o temp/objects/myra_app.o temp/objects/calculator_app.o temp/objects/files_app.o -T linkers/linker.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map
@REM call "arm-none/arm-none-eabi-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/uart.o -T linkers/linker_pi.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map 
@REM call "arm-none/arm-none-eabi-objcopy.bat" -O binary -S temp/elfs/kernel.elf temp/binaries/kernel.img
call "aarch64/aarch64-none-elf-objcopy.bat" -O binary temp/elfs/kernel.elf temp/binaries/kernel8.img
@REM call "qemu/qemu-system-aarch64.bat" -machine virt -cpu cortex-15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio
echo Launching QEMU with SDL + ramfb (fallback) and virtio-gpu
@REM call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -display sdl -device virtio-gpu-pci -device ramfb -kernel temp/elfs/kernel.elf -serial mon:stdio
call "qemu/qemu-system-aarch64.bat" ^
  -machine virt ^
  -cpu cortex-a53 ^
  -m 512M ^
  -display sdl ^
   -device virtio-gpu-device ^
   -device virtio-mouse-device ^
   -device virtio-keyboard-device ^
   -device ramfb ^
   -drive if=none,file=disk.img,id=hd0,format=raw -device virtio-blk-device,drive=hd0 ^
  -kernel temp/elfs/kernel.elf ^
  -serial mon:stdio
@REM call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio