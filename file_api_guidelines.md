1. Overall Architecture

Your system has three layers:

SD Card Hardware
      ↓
SDHCI Driver (block device)
      ↓
Disk Layer (read sector)
      ↓
Simple Filesystem (your directory table)
      ↓
File API (open/read)

Minimal modules:

emmc.c        → SD controller driver
block.c       → sector read abstraction
diskfs.c      → your custom filesystem
file.c        → file read API
2. Disk Layout (From Your Python Script)

Your disk image layout is very simple.

Sector 0      : unused / boot / padding
Sector 1-127  : directory table
Sector 128+   : file data

Visual layout:

+----------------------+
| sector 0             |
| reserved             |
+----------------------+
| sector 1             |
| directory entries    |
+----------------------+
| sector 2             |
| directory entries    |
+----------------------+
| ...                  |
+----------------------+
| sector 128           |
| file data start      |
+----------------------+
3. Directory Entry Structure

From your Python code:

struct.pack('<64sII')

So each entry is:

struct disk_entry
{
    char name[64];
    uint32_t size;
    uint32_t start_sector;
};

Size:

64 + 4 + 4 = 72 bytes

Maximum files:

128

Directory size:

128 * 72 = 9216 bytes

Directory sectors:

9216 / 512 = 18 sectors

So sectors 1–18 actually contain data, but you reserved 1–127, which is fine.

4. File Storage

Each file is stored contiguously.

Example:

fileA
start_sector = 128
size = 2000 bytes

Storage:

sector 128
sector 129
sector 130
sector 131

So reading file = just read sectors sequentially.

This is extremely simple and fast.

5. Minimal SD Driver Guidelines

You only need one capability:

read_sector(lba, buffer)

Sector size = 512 bytes

Your filesystem should never know about SD commands.

6. Minimal SD Driver Structure

Example structure:

emmc_init()
emmc_send_cmd()
emmc_read_block()

Then wrapper:

int sd_read_sector(uint32_t lba, void *buffer)

Example:

int sd_read_sector(uint32_t lba, void *buf)
{
    emmc_send_cmd(CMD17, lba);
    read 512 bytes from DATA register
}
7. Important Care Points (SD Driver)
1. Alignment

Buffer must be:

16-byte aligned

Example:

__attribute__((aligned(16)))
uint8_t sector_buffer[512];
2. Wait for controller ready

Before commands:

wait until CMD_INHIBIT == 0

Before reading:

wait until DATA_AVAILABLE
3. Always clear interrupts

After command:

write interrupt register to clear

Otherwise controller locks.

4. Timeouts

Never wait forever.

Example:

while(!(INTERRUPT & CMD_DONE))
{
    if(timeout)
        error
}
5. Use only SDHC cards

Old cards require byte addressing.

SDHC uses sector addressing.

Check during init:

ACMD41 response

If:

bit 30 = 1

Then card is SDHC.

For your OS: only support SDHC.

8. Block Device Layer

Create a clean abstraction:

int block_read(uint32_t sector, void *buffer)
{
    return sd_read_sector(sector, buffer);
}

Filesystem uses this.

Never talk to SD controller directly.

9. Filesystem Layer

Initialize filesystem by loading directory entries.

Startup:

read sectors 1..18
parse directory entries
store them in RAM

Example:

disk_entry entries[128];

Load:

for sector in directory sectors
    read_sector(sector)
    copy entries
10. File Lookup

Example function:

disk_entry* find_file(const char *name)

Loop through entries:

for i in entries
    if strcmp(entry.name, name)
        return entry
11. File Read

Reading a file is simple.

start_sector
size

Calculate sectors:

sectors = ceil(size / 512)

Read:

for i in sectors
    read_sector(start + i)

Copy to buffer.

12. Example File API

Minimal interface:

int fs_init()

int fs_open(const char *name)

int fs_read(int file, void *buffer)

int fs_read_sector(int file, int sector)

Simplest implementation:

int fs_read_file(const char *name, void *buffer)




14. Performance Guidelines

Important rules:

1. Cache directory

Never reread directory every time.

Load once.

2. Read sequential sectors

SD cards are optimized for sequential reads.

3. Avoid tiny reads

Always read full sector.

4. Avoid malloc

Use static buffers.

15. Safety Rules

Always check:

entry.size != 0
entry.start_sector >= DATA_START_SECTOR
entry.start_sector < total_sectors

Avoid corrupted disk crashing OS.

16. Suggested Constants
SECTOR_SIZE = 512
MAX_FILES = 128
DIR_START = 1
DIR_SECTORS = 18
DATA_START = 128
17. Minimal RAM Usage

Directory:

128 * 72 = 9KB

Buffer:

512 bytes

Very small.