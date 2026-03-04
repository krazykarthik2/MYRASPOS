import os
import shutil

BOOT_DIR = "outputs/boot"
FIRMWARE_DIR = "firmware"
SOCCONFIG_DIR = "SoCConfig"

# delete boot directory if exists
if os.path.exists(BOOT_DIR):
    shutil.rmtree(BOOT_DIR)
    
# Create boot directory
os.makedirs(BOOT_DIR, exist_ok=True)

# Copy kernel
shutil.copy("temp/binaries/kernel8.img",
            f"{BOOT_DIR}/kernel8.img")



# shutil.copy("disk.img", f"{BOOT_DIR}/disk.img")

# Copy firmware
dont_ship = [
    "kernel8.img",
    "kernel_2712.img",
    "kernel7l.img",
    "kernel7.img",
    "kernel.img",
    "config.txt",
    "cmdline.txt"
]

for f in os.listdir(FIRMWARE_DIR):
    if f in dont_ship:
        continue

    src = os.path.join(FIRMWARE_DIR, f)   
    dst = os.path.join(BOOT_DIR, f)

    if not os.path.exists(src):
        raise FileNotFoundError(f"Missing firmware file: {src}")

    if os.path.isdir(src):
        shutil.copytree(src, dst)
    else:
        shutil.copy(src, dst)

copy_these_from_soc_config = [
    "cmdline.txt",
    "config.txt"
]

for f in copy_these_from_soc_config:
    src = os.path.join(SOCCONFIG_DIR, f)
    dst = os.path.join(BOOT_DIR, f)

    if not os.path.exists(src):
        raise FileNotFoundError(f"Missing SoCConfig file: {src}")

    shutil.copy(src, dst)

print("Boot partition ready.")
