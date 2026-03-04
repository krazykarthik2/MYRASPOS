import struct

SECTOR_SIZE = 512
DIR_START_SECTOR = 1
MAX_DISK_FILES = 128
ENTRY_SIZE = 72

def check_disk():
    try:
        with open('disk.img', 'rb') as f:
            f.seek(DIR_START_SECTOR * SECTOR_SIZE)
            for i in range(MAX_DISK_FILES):
                data = f.read(ENTRY_SIZE)
                name_bytes = data[:64].rstrip(b'\0')
                if not name_bytes:
                    continue
                
                size, start = struct.unpack('<II', data[64:72])
                name = name_bytes.decode('utf-8', errors='replace')
                print(f"File {i}: {name}, size={size}, start_sector={start}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    check_disk()
