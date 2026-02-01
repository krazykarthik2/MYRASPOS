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
    # 1. Prepare Directory Table
    # struct disk_entry { char name[64]; uint32_t size; uint32_t start_sector; }
    # Total size of one entry = 64 + 4 + 4 = 72 bytes.
    # THIS IS WRONG. C Compiler struct padding/packing might affect this, but usually
    # char[64] is 64 bytes, uint32 is 4 aligned. 72 is divisible by 4.
    # Let's verify diskfs.h or assume packed for now.
    # In diskfs.c: struct disk_entry { char name[64]; uint32_t size; uint32_t start_sector; };
    # It is likely 72 bytes.
    
    entries = []
    
    # 2. Collect files
    current_sector = DATA_START_SECTOR
    data_blob = bytearray()
    
    if os.path.exists(ASSETS_DIR):
        for fname in os.listdir(ASSETS_DIR):
            fpath = os.path.join(ASSETS_DIR, fname)
            if os.path.isfile(fpath):
                # Target path
                dest_name = TARGET_PREFIX + fname
                if len(dest_name) >= 64:
                    print(f"Warning: Filename too long, skipping: {dest_name}")
                    continue
                
                with open(fpath, 'rb') as f:
                    content = f.read()
                
                size = len(content)
                start_sector = current_sector
                
                # Add to entries
                entries.append({
                    "name": dest_name,
                    "size": size,
                    "start": start_sector
                })
                
                # Append data padded to sector boundary
                data_blob.extend(content)
                padding = (SECTOR_SIZE - (len(content) % SECTOR_SIZE)) % SECTOR_SIZE
                data_blob.extend(b'\0' * padding)
                
                sectors_used = (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE
                current_sector += sectors_used

    # 3. Create Disk Image
    disk_img = bytearray(DISK_SIZE)
    
    # Write Directory Table at DIR_START_SECTOR
    # Directory table is just an array of struct disk_entry
    # We need to serialize this.
    dir_offset = DIR_START_SECTOR * SECTOR_SIZE
    
    for i, entry in enumerate(entries):
        if i >= MAX_DISK_FILES:
            break
        
        # Pack entry
        name_bytes = entry["name"].encode('utf-8')
        name_bytes = name_bytes.ljust(64, b'\0')
        
        # entry_struct = struct.pack('<64sII', name_bytes, entry["size"], entry["start"])
        # Wait, Python struct packing standard:
        # 64s = 64 bytes
        # I = uint32 (4 bytes)
        # Total = 72 bytes.
        
        offset = dir_offset + (i * 72)
        disk_img[offset:offset+64] = name_bytes
        disk_img[offset+64:offset+68] = struct.pack('<I', entry["size"])
        disk_img[offset+68:offset+72] = struct.pack('<I', entry["start"])

    # Write Data
    data_offset = DATA_START_SECTOR * SECTOR_SIZE
    if len(data_blob) > (DISK_SIZE - data_offset):
        print("Error: Disk full!")
        return

    disk_img[data_offset:data_offset+len(data_blob)] = data_blob
    
    # Write to file
    with open('disk.img', 'wb') as f:
        f.write(disk_img)
    
    print(f"Created disk.img with {len(entries)} files.")
    for e in entries:
        print(f"  {e['name']}: {e['size']} bytes @ sector {e['start']}")

if __name__ == "__main__":
    main()
