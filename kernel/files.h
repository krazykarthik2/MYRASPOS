#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  4
#define O_TRUNC  8
#define O_APPEND 16

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct file_stat {
    size_t size;
    int is_dir;
};

void files_init(void);
int files_open(const char *path, int flags);
int files_close(int fd);
int files_read(int fd, void *buf, size_t len);
int files_write(int fd, const void *buf, size_t len);
int files_seek(int fd, int offset, int whence);
int files_stat(const char *path, struct file_stat *st);

#endif
