def check_sector_1():
    try:
        with open('disk.img', 'rb') as f:
            f.seek(512)
            data = f.read(512)
            print("Sector 1 (Directory Table) First 144 bytes:")
            for i in range(2):
                entry = data[i*72 : (i+1)*72]
                name = entry[:64].rstrip(b'\0').decode('utf-8', errors='replace')
                size = int.from_bytes(entry[64:68], 'little')
                start = int.from_bytes(entry[68:72], 'little')
                print(f"Entry {i}: '{name}', size={size}, start_sector={start}")
                print(f"  Hex: {entry.hex()}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    check_sector_1()
