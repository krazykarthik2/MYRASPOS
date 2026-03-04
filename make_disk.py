import struct
import os

# Configuration
DISK_SIZE = 16 * 1024 * 1024  # 16 MB
SECTOR_SIZE = 512
MAX_DISK_FILES = 128
DIR_START_SECTOR = 1
DATA_START_SECTOR = 128
ASSETS_DIR = "assets"
TARGET_PREFIX = "/system/assets/"

def main():
    entries = []
    current_sector = DATA_START_SECTOR
    data_blob = bytearray()
    
    print(f"Scanning directory: {ASSETS_DIR}")
    if os.path.exists(ASSETS_DIR):
        files = os.listdir(ASSETS_DIR)
        print(f"Found {len(files)} files/folders.")
        for fname in files:
            fpath = os.path.join(ASSETS_DIR, fname)
            if os.path.isfile(fpath):
                dest_name = TARGET_PREFIX + fname
                print(f"  Adding: {dest_name}")
                if len(dest_name) >= 64:
                    print(f"    Warning: Filename too long, skipping: {dest_name}")
                    continue
                
                with open(fpath, 'rb') as f:
                    content = f.read()
                
                size = len(content)
                start_sector = current_sector
                
                entries.append({
                    "name": dest_name,
                    "size": size,
                    "start": start_sector
                })
                
                # Append data and pad to sector boundary
                data_blob.extend(content)
                padding = (SECTOR_SIZE - (len(content) % SECTOR_SIZE)) % SECTOR_SIZE
                data_blob.extend(b'\0' * padding)
                
                sectors_used = (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE
                current_sector += sectors_used
    
    disk_img = bytearray(DISK_SIZE)
    
    # Write Directory Table
    dir_offset = DIR_START_SECTOR * SECTOR_SIZE
    for i, entry in enumerate(entries):
        if i >= MAX_DISK_FILES:
            break
        
        # Pack entry: 64s for name, I for size, I for start_sector
        name_bytes = entry["name"].encode('utf-8')
        # name_bytes should be at most 63 bytes to leave room for null terminator?
        # Actually C struct is char[64], so it can be 64 bytes if not used as a string,
        # but diskfs.c uses strcmp, so it MUST be null-terminated.
        # So name_bytes must be at most 63 bytes.
        name_bytes = name_bytes[:63] 
        
        # struct.pack('<64sII', ...) will pad the 64s with nulls if name_bytes is shorter.
        entry_struct = struct.pack('<64sII', name_bytes, entry["size"], entry["start"])
        
        offset = dir_offset + (i * 72)
        disk_img[offset:offset+72] = entry_struct

    # Write Data
    data_offset = DATA_START_SECTOR * SECTOR_SIZE
    if len(data_blob) > (DISK_SIZE - data_offset):
        print("Error: Disk image too small for all files!")
        return

    disk_img[data_offset:data_offset+len(data_blob)] = data_blob
    
    try:
        with open('disk.img', 'wb') as f:
            f.write(disk_img)
            f.flush()
            os.fsync(f.fileno())
        print(f"Created disk.img ({len(disk_img)} bytes) with {len(entries)} files.")
    except OSError as e:
        print(f"Error: Could not write 'disk.img': {e}")
        if e.errno == 22:
            print("Tip: Close QEMU and try again.")

if __name__ == "__main__":
    main()
