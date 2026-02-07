#include "diskfs.h"
#include "virtio.h"
#include "ramfs.h"
#include "uart.h"
#include "kmalloc.h"
#include <string.h>

#define MAX_DISK_FILES 128
#define SECTOR_SIZE 512
#define DIR_START_SECTOR 1
#define DATA_START_SECTOR 128

struct disk_entry {
    char name[64];
    uint32_t size;
    uint32_t start_sector;
} __attribute__((packed));

static struct disk_entry dir_cache[MAX_DISK_FILES];
static int num_files = 0;
static uint32_t next_free_sector = DATA_START_SECTOR;

/* Path lookup cache */
#define DISK_PATH_CACHE_SIZE 16
struct disk_path_cache {
    char name[64];
    int index;
};
static struct disk_path_cache d_cache[DISK_PATH_CACHE_SIZE];
static int d_cache_next = 0;

static int find_file_index(const char *name) {
    /* Check cache */
    for (int i = 0; i < DISK_PATH_CACHE_SIZE; i++) {
        if (d_cache[i].name[0] != '\0' && strcmp(d_cache[i].name, name) == 0) {
            return d_cache[i].index;
        }
    }
    /* Linear search */
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (dir_cache[i].name[0] != '\0' && strcmp(dir_cache[i].name, name) == 0) {
            /* Update cache */
            strncpy(d_cache[d_cache_next].name, name, 63);
            d_cache[d_cache_next].index = i;
            d_cache_next = (d_cache_next + 1) % DISK_PATH_CACHE_SIZE;
            return i;
        }
    }
    return -1;
}

void diskfs_init(void) {
    if (virtio_blk_init() < 0) {
        uart_puts("[diskfs] virtio-blk not found, diskfs disabled.\n");
        return;
    }

    /* Load directory from disk */
    int sectors_to_read = (sizeof(dir_cache) + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (int i = 0; i < sectors_to_read; i++) {
        virtio_blk_rw(DIR_START_SECTOR + i, ((uint8_t *)dir_cache) + i * SECTOR_SIZE, 0);
    }

    /* Count files and find next free sector */
    num_files = 0;
    next_free_sector = DATA_START_SECTOR;
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (dir_cache[i].name[0] != '\0') {
            num_files++;
            uint32_t end = dir_cache[i].start_sector + (dir_cache[i].size + SECTOR_SIZE - 1) / SECTOR_SIZE;
            if (end > next_free_sector) next_free_sector = end;
        }
    }
    uart_puts("[diskfs] initialized. files found="); uart_put_hex(num_files); uart_puts("\n");
}

static void save_dir(void) {
    int sectors_to_write = (sizeof(dir_cache) + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (int i = 0; i < sectors_to_write; i++) {
        virtio_blk_rw(DIR_START_SECTOR + i, ((uint8_t *)dir_cache) + i * SECTOR_SIZE, 1);
    }
}

int diskfs_create(const char *name) {
    if (num_files >= MAX_DISK_FILES) return -1;
    
    // Check if exists
    if (find_file_index(name) >= 0) return 0;

    // Find free slot
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (dir_cache[i].name[0] == '\0') {
            strncpy(dir_cache[i].name, name, 63);
            dir_cache[i].size = 0;
            dir_cache[i].start_sector = next_free_sector;
            num_files++;
            save_dir();
            return 0;
        }
    }
    return -1;
}

int diskfs_write(const char *name, const void *buf, size_t len, size_t offset) {
    int i = find_file_index(name);
    if (i >= 0) {
        /* For simplicity, we overwrite from start if offset is 0, 
           and we don't support sparse or expanding files well yet.
           In this simple diskfs, we just write to consecutive sectors. */
        
        uint32_t start_s = dir_cache[i].start_sector + (offset / SECTOR_SIZE);
        uint32_t n_sectors = (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
        
        uint8_t sector_buf[SECTOR_SIZE];
        const uint8_t *src = (const uint8_t *)buf;
        size_t remaining = len;
        uint32_t curr_s = start_s;

        while (remaining > 0) {
            size_t to_write = (remaining < SECTOR_SIZE) ? remaining : SECTOR_SIZE;
            if (to_write < SECTOR_SIZE) {
                // Partial sector write
                virtio_blk_rw(curr_s, sector_buf, 0); // Read first
                memcpy(sector_buf, src, to_write);
                virtio_blk_rw(curr_s, sector_buf, 1); // Write back
            } else {
                virtio_blk_rw(curr_s, (void *)src, 1);
            }
            src += to_write;
            remaining -= to_write;
            curr_s++;
        }

        if (offset + len > dir_cache[i].size) {
            dir_cache[i].size = offset + len;
            uint32_t end = dir_cache[i].start_sector + (dir_cache[i].size + SECTOR_SIZE - 1) / SECTOR_SIZE;
            if (end > next_free_sector) next_free_sector = end;
            save_dir();
        }
        return len;
    }
    return -1;
}

int diskfs_read(const char *name, void *buf, size_t len, size_t offset) {
    int i = find_file_index(name);
    if (i >= 0) {
        if (offset >= dir_cache[i].size) return 0;
        if (offset + len > dir_cache[i].size) len = dir_cache[i].size - offset;

        uint32_t start_s = dir_cache[i].start_sector + (offset / SECTOR_SIZE);
        uint8_t sector_buf[SECTOR_SIZE];
        uint8_t *dst = (uint8_t *)buf;
        size_t remaining = len;
        uint32_t curr_s = start_s;
        size_t skip = offset % SECTOR_SIZE;

        while (remaining > 0) {
            virtio_blk_rw(curr_s, sector_buf, 0);
            size_t to_copy = SECTOR_SIZE - skip;
            if (to_copy > remaining) to_copy = remaining;
            memcpy(dst, sector_buf + skip, to_copy);
            dst += to_copy;
            remaining -= to_copy;
            curr_s++;
            skip = 0;
        }
        return len;
    }
    return -1;
}

void diskfs_sync_from_ramfs(void) {
    uart_puts("[diskfs] syncing from ramfs...\n");
    char list_buf[1024];
    int count = ramfs_list("/", list_buf, sizeof(list_buf));
    if (count < 0) return;

    char *name = list_buf;
    while (*name) {
        if (!ramfs_is_dir(name)) {
            // Read from ramfs
            uint8_t *tmp = kmalloc(65536); // Assume max 64KB for now
            int read_len = ramfs_read(name, tmp, 65536, 0);
            if (read_len > 0) {
                // Check if file exists in diskfs and has different size/content?
                // For now, simple logic: create/overwrite.
                // Log only if creating
                if (find_file_index(name) < 0) {
                    uart_puts("  syncing NEW: "); uart_puts(name); uart_puts("\n");
                    diskfs_create(name);
                    diskfs_write(name, tmp, read_len, 0);
                } else {
                     // Update? 
                     // uart_puts("  syncing UPDATE: "); uart_puts(name); uart_puts("\n");
                     // diskfs_write(name, tmp, read_len, 0); 
                }
            }
            kfree(tmp);
        }
        name += strlen(name) + 1;
    }
    uart_puts("[diskfs] sync complete.\n");
}

void diskfs_sync_to_ramfs(void) {
    uart_puts("[diskfs] loading from disk to ramfs...\n");
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (dir_cache[i].name[0] != '\0') {
             uart_puts("[diskfs] found file on disk: "); uart_puts(dir_cache[i].name); uart_puts("\n");
              // Create parent dirs if needed?
              // name like "/system/assets/calculator.png"
             // Helper to mkdir -p? 
             // For now assume flat or rely on ramfs logic? 
             // ramfs_create requires parent dir to exist usually.
             
             // Naive load
             int size = dir_cache[i].size;
             uint8_t *buf = kmalloc(size);
             if (buf) {
                 diskfs_read(dir_cache[i].name, buf, size, 0);
                 
                 // Ensure directory exists
                 // Parse path
                 char path_buf[128];
                 strncpy(path_buf, dir_cache[i].name, 63);
                 // Walk and create dirs
                 char *p = path_buf;
                 while (*p == '/') p++; // skip leading /
                 while (*p) {
                     if (*p == '/') {
                         *p = '\0';
                         ramfs_mkdir(path_buf); // Try create
                         *p = '/';
                     }
                     p++;
                 }

                 if (ramfs_create(dir_cache[i].name) == 0) {
                     ramfs_write(dir_cache[i].name, buf, size, 0);
                     uart_puts("  loaded: "); uart_puts(dir_cache[i].name); uart_puts("\n");
                 } else {
                     uart_puts("  failed to create in ramfs: "); uart_puts(dir_cache[i].name); uart_puts("\n");
                 }
                 kfree(buf);
             }
        }
    }
    uart_puts("[diskfs] load complete.\n");
}
