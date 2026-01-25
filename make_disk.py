with open('disk.img', 'wb') as f:
    f.write(b'\0' * 1024 * 1024)
