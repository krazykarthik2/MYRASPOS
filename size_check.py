import os
print(f"disk.img size: {os.path.getsize('disk.img')}")
with open('disk.img', 'rb') as f:
    f.seek(512)
    data = f.read(512)
    print(f"Sector 1 start: {data[:16].hex()}")
