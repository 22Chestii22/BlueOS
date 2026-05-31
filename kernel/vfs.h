#ifndef VFS_H
#define VFS_H

#include "types.h"

#define O_READ  1
#define O_WRITE 2
#define O_CREAT 4

int vfs_mount(const char* name, int device,
              int (*mount)(void*, int),
              int (*read)(void*, const char*, void*, uint32_t),
              int (*write)(void*, const char*, const void*, uint32_t),
              int (*open)(void*, const char*, int),
              int (*close)(void*, int),
              int (*readdir)(void*, const char*, char*, int),
              int (*mkdir)(void*, const char*),
              int (*unlink)(void*, const char*),
              int (*rename)(void*, const char*, const char*));
int vfs_open(const char* path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void* buffer, uint32_t size);
int vfs_write(int fd, const void* buffer, uint32_t size);
int vfs_readdir(const char* path, char* entries, int max_entries);
int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_rename(const char* old_path, const char* new_path);
int vfs_exists(const char* path);

#endif
