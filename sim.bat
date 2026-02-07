call build.bat %1
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
