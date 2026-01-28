#include "files.h"
#include "ramfs.h"
#include "diskfs.h"
#include "kmalloc.h"
#include "lib.h"
#include "uart.h"
#include <string.h>

#define MAX_FDS 32

struct file_desc {
    int used;
    char path[128];
    size_t pos;
    int flags;
};

static struct file_desc fds[MAX_FDS];

void files_init(void) {
    memset(fds, 0, sizeof(fds));
}

static int load_from_disk_if_needed(const char *path) {
    if (ramfs_get_size(path) >= 0) return 0; /* Exists in RAM */

    /* Check diskfs by attempting to read */
    void *tmp = kmalloc(65536);
    if (!tmp) return -1;
    
    int r = diskfs_read(path, tmp, 65536, 0);
    if (r < 0) {
        kfree(tmp);
        return -1;
    }
    
    /* Found in disk, create in RAM */
    ramfs_create(path);
    ramfs_write(path, tmp, r, 0);
    kfree(tmp);
    return 0;
}

int files_open(const char *path, int flags) {
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fds[i].used) { fd = i; break; }
    }
    if (fd == -1) return -1;

    if (load_from_disk_if_needed(path) < 0) {
        /* Not in ram or disk */
        if (flags & O_CREAT) {
            /* Create new empty file */
            if (ramfs_create(path) < 0) {
                return -1;
            }
        } else {
            return -1;
        }
    } else {
        /* Exists */
        if (flags & O_TRUNC) {
            ramfs_remove(path);
            ramfs_create(path);
        }
    }

    fds[fd].used = 1;
    strncpy(fds[fd].path, path, 127);
    fds[fd].path[127] = '\0';
    fds[fd].pos = 0;
    
    if (flags & O_APPEND) {
        int sz = ramfs_get_size(path);
        if (sz >= 0) fds[fd].pos = sz;
    }
    
    fds[fd].flags = flags;
    return fd;
}

int files_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    fds[fd].used = 0;
    return 0;
}

int files_read(int fd, void *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    int r = ramfs_read(fds[fd].path, buf, len, fds[fd].pos);
    if (r > 0) fds[fd].pos += r;
    return r;
}

int files_write(int fd, const void *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    int w = ramfs_write(fds[fd].path, buf, len, fds[fd].pos);
    if (w > 0) {
        /* Write-through persistence */
        /* Since diskfs_write handles offsets naturally, we can write the chunk.
           But we must ensure the file exists on disk. diskfs_create is idempotent-ish check first. */
        diskfs_create(fds[fd].path);
        diskfs_write(fds[fd].path, buf, len, fds[fd].pos);
        /* Advance pos after write logic */
        fds[fd].pos += w;
    }
    return w;
}

int files_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    int size = ramfs_get_size(fds[fd].path);
    if (size < 0) return -1;
    
    int new_pos = (int)fds[fd].pos;
    if (whence == SEEK_SET) new_pos = offset;
    else if (whence == SEEK_CUR) new_pos += offset;
    else if (whence == SEEK_END) new_pos = size + offset;
    
    if (new_pos < 0) new_pos = 0;
    /* allow seeking past end? */
    fds[fd].pos = new_pos;
    return new_pos;
}

int files_stat(const char *path, struct file_stat *st) {
    if (load_from_disk_if_needed(path) < 0) return -1;
    int s = ramfs_get_size(path);
    if (s < 0) return -1;
    st->size = s;
    st->is_dir = ramfs_is_dir(path);
    return 0;
}
