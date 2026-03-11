"""
make_real_disk.py  --  Flash MYRAS OS to a real SD card (Windows only)

Flow:
  1. List logical drives → pick one (C: blocked) → copy outputs/boot/* to it
  2. List physical disks → pick one
  3. List partitions of that disk → pick the raw/data partition
  4. Write diskfs image to sector 0 of the chosen partition

The EMMC driver reads the MBR at boot to locate the partition by type/index,
so there is no hardcoded offset in the kernel code.

Requires: Windows, admin privileges for raw write
"""

import os
import sys
import ctypes
import shutil
import struct
import subprocess

# ── Config ───────────────────────────────────────────────────────────
BOOT_DIR       = "outputs\\boot"
ASSETS_DIR     = "assets"
SECTOR_SIZE    = 512
MAX_DISK_FILES = 128
DIR_START      = 1
DATA_START     = 128
# DISK_IMG_SIZE is computed dynamically from asset sizes (see build_disk_image)
DISK_IMG_MIN   = 32 * 1024 * 1024   # 32 MB minimum

# ── Helpers ───────────────────────────────────────────────────────────

def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except Exception:
        return False

def list_logical_drives():
    bitmask = ctypes.windll.kernel32.GetLogicalDrives()
    drives = []
    for i in range(26):
        if bitmask & (1 << i):
            drives.append(chr(ord('A') + i) + ":\\")
    return drives

def get_drive_info(letter):
    buf_label = ctypes.create_unicode_buffer(261)
    buf_fs    = ctypes.create_unicode_buffer(261)
    free_bytes  = ctypes.c_ulonglong(0)
    total_bytes = ctypes.c_ulonglong(0)
    ok = ctypes.windll.kernel32.GetVolumeInformationW(
        letter, buf_label, 261, None, None, None, buf_fs, 261)
    ctypes.windll.kernel32.GetDiskFreeSpaceExW(
        letter, None, ctypes.byref(total_bytes), ctypes.byref(free_bytes))
    if not ok:
        return None
    return (buf_label.value or "(no label)", buf_fs.value or "?",
            free_bytes.value, total_bytes.value)

def list_physical_disks():
    disks = []
    try:
        out = subprocess.check_output(
            ["wmic", "diskdrive", "get", "Index,Model,Size,MediaType", "/format:csv"],
            stderr=subprocess.DEVNULL).decode(errors="replace")
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        if len(lines) < 2:
            return disks
        header = [h.strip().lower() for h in lines[0].split(",")]
        for row in lines[1:]:
            fields = row.split(",")
            if len(fields) < len(header):
                continue
            d = dict(zip(header, fields))
            idx   = d.get("index", "?").strip()
            model = d.get("model", "?").strip()
            size  = d.get("size", "0").strip()
            mtype = d.get("mediatype", "").strip()
            try:
                size_gb = int(size) / (1024**3)
            except ValueError:
                size_gb = 0
            disks.append({"index": idx, "model": model,
                          "size_gb": size_gb, "type": mtype,
                          "path": f"\\\\.\\PhysicalDrive{idx}"})
    except Exception as e:
        print(f"[warn] wmic diskdrive failed: {e}")
    return disks

def list_partitions(disk_index):
    """
    List partitions on a given disk using wmic.
    Returns list of dicts: index, name, type, start_lba, size_bytes
    """
    parts = []
    try:
        out = subprocess.check_output(
            ["wmic", "partition",
             "where", f"DiskIndex={disk_index}",
             "get", "Index,Name,Type,StartingOffset,Size",
             "/format:csv"],
            stderr=subprocess.DEVNULL).decode(errors="replace")
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        if len(lines) < 2:
            return parts
        header = [h.strip().lower() for h in lines[0].split(",")]
        for row in lines[1:]:
            fields = row.split(",")
            if len(fields) < len(header):
                continue
            d = dict(zip(header, fields))
            pidx  = d.get("index", "?").strip()
            pname = d.get("name",  "Partition").strip()
            ptype = d.get("type",  "?").strip()
            try:
                start_bytes = int(d.get("startingoffset", "0").strip())
                start_lba   = start_bytes // SECTOR_SIZE
            except ValueError:
                start_bytes, start_lba = 0, 0
            try:
                size_bytes = int(d.get("size", "0").strip())
                size_mb    = size_bytes / (1024**2)
            except ValueError:
                size_bytes, size_mb = 0, 0
            parts.append({
                "index":      pidx,
                "name":       pname,
                "type":       ptype,
                "start_lba":  start_lba,
                "start_bytes": start_bytes,
                "size_bytes": size_bytes,
                "size_mb":    size_mb,
            })
    except Exception as e:
        print(f"[warn] wmic partition failed: {e}")
    return parts

def pick(prompt, options, key_fn, banned_fn=None):
    print()
    print(prompt)
    for i, opt in enumerate(options):
        flag = "✗ " if (banned_fn and banned_fn(opt)) else "  "
        print(f"  {flag}[{i+1}] {key_fn(opt)}")
    while True:
        raw = input("\nEnter number (or 'q' to quit): ").strip()
        if raw.lower() == 'q':
            print("Aborted.")
            sys.exit(0)
        try:
            n = int(raw)
            if 1 <= n <= len(options):
                chosen = options[n - 1]
                if banned_fn and banned_fn(chosen):
                    print("That selection is not allowed. Pick another.")
                    continue
                return chosen
        except ValueError:
            pass
        print("Invalid selection, try again.")

# ── Disk layout visualization ─────────────────────────────────────────

def visualize_write(disk, all_parts, target_part, disk_image_bytes):
    """
    Print a visual disk-map showing where we're writing relative to the
    whole physical disk and all other partitions.
    """
    total_sectors = 0
    for p in all_parts:
        end = p['start_lba'] + (p['size_bytes'] // SECTOR_SIZE)
        if end > total_sectors:
            total_sectors = end
    if total_sectors == 0:
        total_sectors = max(p['start_lba'] + 1 for p in all_parts)

    write_start_lba  = target_part['start_lba']
    write_sectors    = (len(disk_image_bytes) + SECTOR_SIZE - 1) // SECTOR_SIZE
    write_end_lba    = write_start_lba + write_sectors
    write_start_byte = target_part['start_bytes']
    write_end_byte   = write_start_byte + len(disk_image_bytes)

    BAR = 60  # total bar width in chars

    def lba_to_bar(lba):
        return max(0, min(BAR, int(lba * BAR / total_sectors)))

    print()
    print("  ╔" + "═" * (BAR + 2) + "╗")
    print(f"  ║  Disk: {disk['model'][:BAR-8]:^{BAR-2}}  ║")
    print(f"  ║  {disk['size_gb']:.1f} GB  ({total_sectors:,} sectors)  {' '*(BAR-30)}║")
    print("  ╠" + "═" * (BAR + 2) + "╣")

    # MBR
    mbr_bar = " " * BAR
    print(f"  ║ [MBR]{mbr_bar[5:]} ║")
    print("  ╠" + "─" * (BAR + 2) + "╣")

    # Each partition
    colors = ["░", "▒", "▓", "█"]
    for idx, p in enumerate(all_parts):
        if p['size_bytes'] == 0:
            continue
        lba_s = p['start_lba']
        lba_e = lba_s + (p['size_bytes'] // SECTOR_SIZE)
        bar_s = lba_to_bar(lba_s)
        bar_e = lba_to_bar(lba_e)
        fill  = colors[idx % len(colors)] * max(1, bar_e - bar_s)
        bar   = " " * bar_s + fill + " " * max(0, BAR - bar_s - len(fill))
        marker = " ◀ WRITE TARGET" if p is target_part else ""
        print(f"  ║ {bar} ║ P{p['index']} {p['type'][:10]} {p['size_mb']:.0f}MB{marker}")

    print("  ╠" + "═" * (BAR + 2) + "╣")

    # Write region zoomed in
    print()
    print("  ┌─ Write region detail " + "─" * (BAR - 20) + "┐")
    print(f"  │  Partition start : sector {write_start_lba:>10,}  byte {write_start_byte:>14,}  │")
    print(f"  │  Write end       : sector {write_end_lba:>10,}  byte {write_end_byte:>14,}  │")
    print(f"  │  Bytes written   : {len(disk_image_bytes):>10,}  ({len(disk_image_bytes)/1024/1024:.2f} MB)          │")
    print(f"  │  Sectors written : {write_sectors:>10,}                                │")
    print("  └" + "─" * (BAR + 2) + "┘")

    # Zoomed bar for write region within the target partition
    part_sectors = target_part['size_bytes'] // SECTOR_SIZE
    if part_sectors > 0:
        pct_used = write_sectors * 100 // max(1, part_sectors)
        filled   = pct_used * BAR // 100
        bar_fill = "█" * filled + "░" * (BAR - filled)
        print(f"\n  Partition usage: [{bar_fill}] {pct_used}% ({write_sectors:,}/{part_sectors:,} sects)")
    print()

# ── Build diskfs image ────────────────────────────────────────────────

def build_disk_image():
    entries = []
    current_sector = DATA_START
    data_blob = bytearray()

    print(f"\n[diskfs] Scanning: {ASSETS_DIR}")
    if os.path.exists(ASSETS_DIR):
        TARGET_PREFIX = "/system/assets/"
        for fname in sorted(os.listdir(ASSETS_DIR)):
            fpath = os.path.join(ASSETS_DIR, fname)
            if not os.path.isfile(fpath):
                continue
            dest = TARGET_PREFIX + fname
            if len(dest) >= 64:
                print(f"  skip (name too long): {dest}")
                continue
            with open(fpath, "rb") as f:
                content = f.read()
            entries.append({"name": dest, "size": len(content), "start": current_sector})
            data_blob.extend(content)
            pad = (SECTOR_SIZE - (len(content) % SECTOR_SIZE)) % SECTOR_SIZE
            data_blob.extend(b'\x00' * pad)
            current_sector += (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE
            print(f"  + {dest}  ({len(content):,} bytes)")

    # Auto-size: header + data + 20% headroom, minimum DISK_IMG_MIN
    header_bytes = DATA_START * SECTOR_SIZE           # 128 sectors = 64 KB
    needed = header_bytes + len(data_blob)
    disk_img_size = max(DISK_IMG_MIN, int(needed * 1.2))
    # Round up to sector boundary
    disk_img_size = ((disk_img_size + SECTOR_SIZE - 1) // SECTOR_SIZE) * SECTOR_SIZE
    print(f"[diskfs] Image size: {disk_img_size:,} bytes ({disk_img_size//1024//1024} MB)")
    img = bytearray(disk_img_size)

    # Directory table
    dir_off = DIR_START * SECTOR_SIZE
    for i, e in enumerate(entries[:MAX_DISK_FILES]):
        name_b = e["name"].encode("utf-8")[:63]
        rec = struct.pack("<64sII", name_b, e["size"], e["start"])
        img[dir_off + i*72 : dir_off + i*72 + 72] = rec

    # File data
    data_off = DATA_START * SECTOR_SIZE
    if data_off + len(data_blob) > disk_img_size:
        print("[diskfs] ERROR: image overflow even after auto-sizing!")
        sys.exit(1)
    img[data_off : data_off + len(data_blob)] = data_blob

    print(f"[diskfs] Built: {len(entries)} file(s), image size {len(img):,} bytes")
    return bytes(img)

# ── Copy boot files to logical drive ──────────────────────────────────

def copy_boot_to_drive(drive_letter):
    if not os.path.isdir(BOOT_DIR):
        print(f"[boot] ERROR: '{BOOT_DIR}' not found. Build first.")
        return False
    files = [f for f in os.listdir(BOOT_DIR) if os.path.isfile(os.path.join(BOOT_DIR, f))]
    root = drive_letter.rstrip("\\") + "\\"
    print(f"\n[boot] Copying {len(files)} file(s) to {root}")
    for fname in files:
        src = os.path.join(BOOT_DIR, fname)
        dst = os.path.join(root, fname)
        shutil.copy2(src, dst)
        print(f"  → {dst}")
    print("[boot] Done.")
    return True

# ── Write diskfs image to start of a partition ────────────────────────

def write_to_partition(disk_path, partition, disk_image_bytes):
    """
    Write disk_image_bytes starting at the physical sector where
    the chosen partition begins (byte offset = partition['start_bytes']).
    """
    if not is_admin():
        print("\n[raw] ERROR: Raw disk write requires Administrator privileges.")
        print("      Right-click the terminal and choose 'Run as administrator',")
        print("      then run build.bat --real again.")
        return False

    byte_offset = partition["start_bytes"]
    start_lba   = partition["start_lba"]
    total       = len(disk_image_bytes)

    print(f"\n[raw] Opening {disk_path} ...")
    print(f"[raw] Target partition:  {partition['name']}  (type: {partition['type']})")
    print(f"[raw] Partition start:   sector {start_lba}  (byte offset {byte_offset})")
    print(f"[raw] Writing:           {total:,} bytes  ({total // (1024*1024)} MB)")

    GENERIC_WRITE    = 0x40000000
    FILE_SHARE_READ  = 0x00000001
    FILE_SHARE_WRITE = 0x00000002
    OPEN_EXISTING    = 3
    INVALID_HANDLE   = ctypes.c_void_p(-1).value
    kernel32 = ctypes.windll.kernel32

    handle = kernel32.CreateFileW(
        disk_path, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        None, OPEN_EXISTING, 0, None)
    if handle == INVALID_HANDLE:
        err = ctypes.GetLastError()
        print(f"[raw] ERROR: Cannot open disk (error {err})")
        if err == 5:
            print("      Access denied — run as Administrator.")
        return False

    # Seek to partition start
    hi = ctypes.c_long(byte_offset >> 32)
    lo = byte_offset & 0xFFFFFFFF
    res = kernel32.SetFilePointer(handle, lo, ctypes.byref(hi), 0)
    if res == 0xFFFFFFFF and ctypes.GetLastError() != 0:
        print("[raw] ERROR: Seek to partition start failed.")
        kernel32.CloseHandle(handle)
        return False

    # Write in 512 KB chunks
    CHUNK = 512 * 1024
    written_total = 0
    buf = (ctypes.c_char * CHUNK)()
    written_ptr = ctypes.c_ulong(0)

    while written_total < total:
        chunk_size = min(CHUNK, total - written_total)
        ctypes.memmove(buf, disk_image_bytes[written_total:written_total + chunk_size], chunk_size)
        ok = kernel32.WriteFile(handle, buf, chunk_size, ctypes.byref(written_ptr), None)
        if not ok:
            err = ctypes.GetLastError()
            print(f"\n[raw] ERROR: WriteFile failed at +{written_total} bytes (error {err})")
            kernel32.CloseHandle(handle)
            return False
        written_total += written_ptr.value
        pct = written_total * 100 // total
        bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
        print(f"  [{bar}] {pct:3d}%  {written_total:,}/{total:,} bytes\r",
              end="", flush=True)

    print(f"\n[raw] Write complete: {written_total:,} bytes written to partition start.")
    kernel32.CloseHandle(handle)
    return True

# ── Main ──────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  MYRAS OS  ──  Real SD Card Flasher")
    print("=" * 60)

    # ── 1. Logical drive picker (boot files) ──────────────────────────
    all_drives = list_logical_drives()
    if not all_drives:
        print("No logical drives found.")
        sys.exit(1)

    print("\n── Logical Drives (choose where to copy boot files) ──")
    print("  C:\\ is protected and cannot be selected.")
    drive_infos = []
    for d in all_drives:
        info = get_drive_info(d)
        if info:
            label, fs, free, total = info
            drive_infos.append({
                "letter": d, "label": label, "fs": fs,
                "free_gb":  free  / (1024**3),
                "total_gb": total / (1024**3),
            })
        else:
            drive_infos.append({"letter": d, "label": "?", "fs": "?",
                                 "free_gb": 0, "total_gb": 0})

    def drive_desc(d):
        return (f"{d['letter']}  [{d['label']}]  {d['fs']}  "
                f"{d['free_gb']:.1f} GB free / {d['total_gb']:.1f} GB")

    selected_drive = pick(
        "Select a logical drive for boot files:",
        drive_infos,
        drive_desc,
        banned_fn=lambda d: d["letter"].upper().startswith("C:"),
    )
    print(f"\n  → Boot drive: {selected_drive['letter']}")

    # ── 2. Physical disk picker ───────────────────────────────────────
    phys_disks = list_physical_disks()
    selected_disk = None
    selected_part = None

    if not phys_disks:
        print("\n[raw] No physical disks found via wmic. Raw write skipped.")
    else:
        print("\n── Physical Disks (choose where to write diskfs) ──")
        print("  *** WARNING: Wrong disk = data loss! ***")

        def disk_desc(d):
            return (f"PhysicalDrive{d['index']}  {d['model']}"
                    f"  {d['size_gb']:.1f} GB  [{d['type']}]")

        selected_disk = pick(
            "Select the physical disk (SD card):",
            phys_disks,
            disk_desc,
            banned_fn=lambda d: "intel" in d["model"].lower(),
        )
        print(f"\n  → Disk: {selected_disk['path']}  ({selected_disk['model']})")

        # ── 3. Partition picker ───────────────────────────────────────
        parts = list_partitions(selected_disk["index"])
        if not parts:
            print("[raw] No partitions found on that disk. Raw write skipped.")
            selected_disk = None
        else:
            print("\n── Partitions on selected disk ──")

            def part_desc(p):
                return (f"Partition {p['index']}  {p['name']}"
                        f"  type: [{p['type']}]"
                        f"  start: sector {p['start_lba']}"
                        f"  size: {p['size_mb']:.1f} MB")

            selected_part = pick(
                "Select the RAW / data partition to write diskfs into:",
                parts,
                part_desc,
            )
            print(f"\n  → Partition: {selected_part['name']}"
                  f"  (sector {selected_part['start_lba']}"
                  f",  {selected_part['size_mb']:.1f} MB)")

            # ── Confirmation ──────────────────────────────────────────
            print(f"\n  !! This will OVERWRITE the beginning of partition:")
            print(f"     {selected_part['name']} on {selected_disk['path']}"
                  f"  ({selected_disk['model']})")
            print(f"     Starting at byte offset {selected_part['start_bytes']}"
                  f"  (sector {selected_part['start_lba']})")
            confirm = input("\n  Type 'yes' to confirm: ").strip().lower()
            if confirm != "yes":
                print("Aborted — nothing was written.")
                sys.exit(0)

    # ── Build diskfs image ────────────────────────────────────────────
    disk_image = build_disk_image()

    # ── Copy boot files ───────────────────────────────────────────────
    copy_boot_to_drive(selected_drive["letter"])

    # ── Write raw diskfs to partition ─────────────────────────────────
    if selected_disk and selected_part:
        # Show disk map before writing
        visualize_write(selected_disk, parts, selected_part, disk_image)
        ok = write_to_partition(selected_disk["path"], selected_part, disk_image)
        if not ok:
            print("\n[raw] Raw write failed. Boot files were still copied successfully.")
            sys.exit(1)

    print("\n✓ All done!  Safely eject the SD card before removing it.")
    if selected_part:
        print(f"  EMMC driver will find diskfs at sector {selected_part['start_lba']}"
              f" via MBR partition table.")

    # Auto-eject the drive
    try:
        drive_letter = selected_drive["letter"].rstrip("\\")
        print(f"\n[eject] Attempting to eject {drive_letter}...")
        subprocess.run(
            ["powershell", "-Command", f"$driveEject = New-Object -comObject Shell.Application; $driveEject.Namespace(17).ParseName('{drive_letter}').InvokeVerb('Eject')"],
            check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        print("  Eject command sent.")
    except Exception as e:
        print(f"  [warn] Could not auto-eject: {e}")

if __name__ == "__main__":
    main()
