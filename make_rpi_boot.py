import os
import shutil

BOOT_DIR = "outputs/boot"
FIRMWARE_DIR = "firmware"

os.makedirs(BOOT_DIR, exist_ok=True)

# Copy kernel
shutil.copy("temp/binaries/kernel8.img",
            f"{BOOT_DIR}/kernel8.img")

# Copy config
shutil.copy("config.txt",
            f"{BOOT_DIR}/config.txt")


# shutil.copy("disk.img", f"{BOOT_DIR}/disk.img")

# Copy firmware
required_files = [
    "start.elf",
    "fixup.dat",
    "bcm2710-rpi-zero-2-w.dtb"
]

for f in required_files:
    src = os.path.join(FIRMWARE_DIR, f)   
    dst = os.path.join(BOOT_DIR, f)

    if not os.path.exists(src):
        raise FileNotFoundError(f"Missing firmware file: {src}")

    shutil.copy(src, dst)

print("Boot partition ready.")
