import os
assets = 'assets'
print(f"Checking assets in {os.getcwd()}")
if os.path.exists(assets):
    print(f"Found {assets}")
    files = os.listdir(assets)
    print(f"Files: {files}")
    for f in files:
        full = os.path.join(assets, f)
        print(f"  {f}: isfile={os.path.isfile(full)}, size={os.path.getsize(full) if os.path.isfile(full) else 'N/A'}")
else:
    print(f"{assets} NOT FOUND")
