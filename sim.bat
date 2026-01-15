
@echo ON
del /F /Q temp\objects\*.o
del /F /Q temp\elfs\*.elf
del /F /Q temp\binaries\*.img
del /F /Q temp\maps\*.map




call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c boot/start.S -o temp/objects/start.o 
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/kernel.c -o temp/objects/kernel.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/uart.c -o temp/objects/uart.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/palloc.c -o temp/objects/palloc.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/kmalloc.c -o temp/objects/kmalloc.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/ramfs.c -o temp/objects/ramfs.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/lib.c -o temp/objects/lib.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/syscall.c -o temp/objects/syscall.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/sched.c -o temp/objects/sched.o
call "arm-none/arm-none-eabi-gcc.bat"  -fno-builtin -fno-merge-constants -fno-common  -ffreestanding -nostdlib  -nostartfiles -mcpu=cortex-a15 -marm -Wall -Wextra  -Wmissing-prototypes -c kernel/panic.c -o temp/objects/panic.o
call "arm-none/arm-none-eabi-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/uart.o temp/objects/palloc.o temp/objects/kmalloc.o temp/objects/ramfs.o temp/objects/lib.o temp/objects/syscall.o temp/objects/sched.o temp/objects/panic.o -T linkers/linker.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map 
@REM call "arm-none/arm-none-eabi-ld.bat" temp/objects/start.o temp/objects/kernel.o temp/objects/uart.o -T linkers/linker_pi.ld -o temp/elfs/kernel.elf -Map temp/maps/kernel.map 
@REM call "arm-none/arm-none-eabi-objcopy.bat" -O binary -S temp/elfs/kernel.elf temp/binaries/kernel.img
call "arm-none/arm-none-eabi-objcopy.bat" -O binary temp/elfs/kernel.elf temp/binaries/kernel8.img
@REM call "qemu/qemu-system-aarch64.bat" -machine virt -cpu cortex-15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio
call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -nographic -kernel temp/elfs/kernel.elf -serial mon:stdio 
@REM call "qemu/qemu-system-arm.bat" -machine virt -cpu cortex-a15 -nographic -kernel temp/binaries/kernel8.img -serial mon:stdio 