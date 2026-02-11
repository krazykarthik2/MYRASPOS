call build.bat 
call "../../qemu/qemu-system-aarch64.bat" -M raspi3b -kernel kernel8.img  -serial mon:stdio