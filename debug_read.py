try:
    with open('disk.img', 'rb') as f:
        # Sector 0 is likely empty?
        # Sector 1 should have directory table
        f.seek(512)
        data = f.read(512)
        print(f"Sector 1 hex: {data[:64].hex()}")
        print(f"Sector 1 content: {data[:64]}")
except Exception as e:
    print(f"Error: {e}")
