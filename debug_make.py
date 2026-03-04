import struct
import os

DISK_SIZE = 16 * 1024 * 1024
SECTOR_SIZE = 512
MAX_DISK_FILES = 128
DIR_START_SECTOR = 1
DATA_START_SECTOR = 128
ASSETS_DIR = "assets"
TARGET_PREFIX = "/system/assets/"

def debug_make():
    log = []
    log.append(f"CWD: {os.getcwd()}")
    entries = []
    current_sector = DATA_START_SECTOR
    data_blob = bytearray()
    
    if os.path.exists(ASSETS_DIR):
        log.append(f"Found {ASSETS_DIR}")
        for fname in os.listdir(ASSETS_DIR):
            fpath = os.path.join(ASSETS_DIR, fname)
            if os.path.isfile(fpath):
                dest_name = TARGET_PREFIX + fname
                log.append(f"Adding {fname} as {dest_name}")
                with open(fpath, 'rb') as f:
                    content = f.read()
                entries.append({"name": dest_name, "size": len(content), "start": current_sector})
                data_blob.extend(content)
                padding = (SECTOR_SIZE - (len(content) % SECTOR_SIZE)) % SECTOR_SIZE
                data_blob.extend(b'\0' * padding)
                current_sector += (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE
    else:
        log.append(f"{ASSETS_DIR} NOT FOUND")

    disk_img = bytearray(DISK_SIZE)
    dir_offset = DIR_START_SECTOR * SECTOR_SIZE
    for i, entry in enumerate(entries):
        if i >= MAX_DISK_FILES: break
        name_bytes = entry["name"].encode('utf-8').ljust(64, b'\0')
        offset = dir_offset + (i * 72)
        disk_img[offset:offset+64] = name_bytes
        disk_img[offset+64:offset+68] = struct.pack('<I', entry["size"])
        disk_img[offset+68:offset+72] = struct.pack('<I', entry["start"])

    data_offset = DATA_START_SECTOR * SECTOR_SIZE
    disk_img[data_offset:data_offset+len(data_blob)] = data_blob
    
    try:
        with open('disk.img', 'wb') as f:
            f.write(disk_img)
        log.append("Wrote disk.img")
    except Exception as e:
        log.append(f"Error writing disk.img: {e}")

    with open('debug_make_log.txt', 'w') as f:
        f.write('\n'.join(log))

if __name__ == "__main__":
    debug_make()
