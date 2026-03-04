import subprocess
import time
import os
import sys

def capture_qemu():
    cmd = [
        r'C:\msys64\ucrt64\bin\qemu-system-aarch64.exe',
        '-M', 'virt',
        '-cpu', 'cortex-a53',
        '-m', '512M',
        '-kernel', r'temp\binaries\kernel8.img',
        '-drive', 'if=none,file=disk.img,id=hd0,format=raw',
        '-device', 'virtio-blk-device,drive=hd0',
        "-nographic",
        "-serial", "mon:stdio"
    ]
    
    print(f"Starting QEMU in nographic mode...")
    try:
        # Run subprocess and let it print directly to stdout
        result = subprocess.run(cmd, text=True, timeout=20)
        print("QEMU finished normally.")
    except subprocess.TimeoutExpired:
        print("QEMU timed out (expected)")
    except Exception as e:
        print(f"Error: {e}")
    
    print("Done.")
    sys.stdout.flush()

if __name__ == "__main__":
    capture_qemu()
    import sys
    sys.stdout.flush()
