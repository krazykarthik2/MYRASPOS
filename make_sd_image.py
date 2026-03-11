"""
make_sd_image.py  –  Combine boot partition + disk.img into a single raw SD card image

Layout:
  Sector 0           : MBR (partition table)
  Sector 2048        : FAT32 boot partition start (firmware + kernel8.img)
  Sector 65536 (32MB): disk.img raw data (used directly by diskfs via EMMC driver)

Usage:
  python make_sd_image.py

Assumes:
  - outputs/boot/ contains the boot partition files (from make_rpi_boot.py)
  - disk.img exists in the project root (from make_disk.py)
"""
import os
import struct

BOOT_DIR     = "outputs/boot"
DISK_IMG     = "disk.img"
OUTPUT       = "outputs/sdcard.img"
SECTOR_SIZE  = 512
DISK_OFFSET  = 65536                          # sector offset for disk.img
DISK_BYTE_OFF = DISK_OFFSET * SECTOR_SIZE     # 32 MB
TOTAL_SIZE   = 64 * 1024 * 1024               # 64 MB image (expandable)

def main():
    if not os.path.isdir(BOOT_DIR):
        print(f"ERROR: {BOOT_DIR} not found.  Run make_rpi_boot.py first.")
        return
    if not os.path.isfile(DISK_IMG):
        print(f"ERROR: {DISK_IMG} not found.  Run make_disk.py first.")
        return

    # ---- collect boot files ----
    boot_files = {}
    for fname in os.listdir(BOOT_DIR):
        fpath = os.path.join(BOOT_DIR, fname)
        if os.path.isfile(fpath):
            with open(fpath, "rb") as f:
                boot_files[fname] = f.read()

    # ---- read disk.img ----
    with open(DISK_IMG, "rb") as f:
        disk_data = f.read()

    # ---- build raw image ----
    img_size = max(TOTAL_SIZE, DISK_BYTE_OFF + len(disk_data))
    img = bytearray(img_size)

    # Write disk.img at raw offset
    img[DISK_BYTE_OFF:DISK_BYTE_OFF + len(disk_data)] = disk_data

    # Write a simple MBR with two partitions:
    #   Partition 1: FAT32, sector 2048 → sector 65535  (~31 MB)
    #   Partition 2: raw,   sector 65536 → end
    boot_part_start  = 2048
    boot_part_sectors = DISK_OFFSET - boot_part_start     # 63488 sectors
    disk_part_start   = DISK_OFFSET
    disk_part_sectors = (len(disk_data) + SECTOR_SIZE - 1) // SECTOR_SIZE

    def pack_partition(status, ptype, start_lba, size_lba):
        """Pack a 16-byte MBR partition entry (CHS fields set to 0)."""
        return struct.pack('<BBBBBBBBII',
                           status, 0, 0, 0,      # status, CHS start
                           ptype, 0, 0, 0,        # type, CHS end
                           start_lba, size_lba)

    # MBR at sector 0
    mbr = bytearray(512)
    # Partition 1: FAT32 LBA (type 0x0C)
    mbr[446:446+16] = pack_partition(0x80, 0x0C, boot_part_start, boot_part_sectors)
    # Partition 2: raw data (type 0xDA – non-FS data)
    mbr[462:462+16] = pack_partition(0x00, 0xDA, disk_part_start, disk_part_sectors)
    # MBR signature
    mbr[510] = 0x55
    mbr[511] = 0xAA
    img[0:512] = mbr

    # ---- Write boot files into the boot partition area ----
    # We write them sequentially starting at the boot partition offset.
    # NOTE: This is a RAW copy, NOT a proper FAT32 filesystem.
    # For a proper boot partition, the user should format the SD card's
    # first partition as FAT32 and copy the files manually, OR use a
    # tool like mtools.
    #
    # For automated flashing, the recommended workflow is:
    #   1. Copy outputs/boot/* files to the FAT32 partition on the SD card
    #   2. dd if=disk.img of=/dev/sdX seek=65536 bs=512
    #
    # This script creates the raw image for reference / dd flashing.

    boot_offset = boot_part_start * SECTOR_SIZE
    for fname, data in boot_files.items():
        # Store files linearly (not FAT32 — for documentation/debug)
        end = boot_offset + len(data)
        if end <= DISK_BYTE_OFF:
            img[boot_offset:end] = data
            boot_offset = end
            # Pad to sector boundary
            pad = (SECTOR_SIZE - (boot_offset % SECTOR_SIZE)) % SECTOR_SIZE
            boot_offset += pad

    os.makedirs(os.path.dirname(OUTPUT) or '.', exist_ok=True)
    with open(OUTPUT, "wb") as f:
        f.write(img)

    print(f"Created {OUTPUT}")
    print(f"  Total size:     {len(img)} bytes ({len(img)//1024//1024} MB)")
    print(f"  Boot partition: sector {boot_part_start} – {boot_part_start + boot_part_sectors - 1}")
    print(f"  Disk data:      sector {disk_part_start} – {disk_part_start + disk_part_sectors - 1}")
    print()
    print("To flash to SD card:")
    print("  1. Format partition 1 as FAT32 and copy outputs/boot/* files")
    print(f"  2. dd if={DISK_IMG} of=/dev/sdX seek={DISK_OFFSET} bs={SECTOR_SIZE}")
    print("  OR: dd if=outputs/sdcard.img of=/dev/sdX bs=1M")

if __name__ == "__main__":
    main()
