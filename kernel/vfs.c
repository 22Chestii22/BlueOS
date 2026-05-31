#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "vfs.h"

#define MAX_FILESYSTEMS 8
#define MAX_OPEN_FILES 64

typedef struct filesystem
{
    char name[16];
    int (*mount)(struct filesystem* fs, int device);
    int (*read)(struct filesystem* fs, const char* path, void* buffer, uint32_t size);
    int (*write)(struct filesystem* fs, const char* path, const void* buffer, uint32_t size);
    int (*open)(struct filesystem* fs, const char* path, int flags);
    int (*close)(struct filesystem* fs, int fd);
    int (*readdir)(struct filesystem* fs, const char* path, char* entries, int max_entries);
    int (*mkdir)(struct filesystem* fs, const char* path);
    int (*unlink)(struct filesystem* fs, const char* path);
    int (*rename)(struct filesystem* fs, const char* old_path, const char* new_path);
    void* private_data;
    int device;
} filesystem_t;

typedef struct file_descriptor
{
    int used;
    int fs_id;
    uint32_t position;
    uint32_t size;
    char path[256];
    int flags;
} file_descriptor_t;

static filesystem_t filesystems[MAX_FILESYSTEMS];
static int num_filesystems = 0;

static file_descriptor_t open_files[MAX_OPEN_FILES];
static int next_fd = 3;

int vfs_mount(const char* name, int device,
              int (*mount)(void*, int),
              int (*read)(void*, const char*, void*, uint32_t),
              int (*write)(void*, const char*, const void*, uint32_t),
              int (*open)(void*, const char*, int),
              int (*close)(void*, int),
              int (*readdir)(void*, const char*, char*, int),
              int (*mkdir)(void*, const char*),
              int (*unlink)(void*, const char*),
              int (*rename)(void*, const char*, const char*))
{
    if (num_filesystems >= MAX_FILESYSTEMS) return -1;

    filesystem_t* fs = &filesystems[num_filesystems];
    strncpy(fs->name, name, 15);
    fs->device = device;
    fs->mount = (void*)mount;
    fs->read = (void*)read;
    fs->write = (void*)write;
    fs->open = (void*)open;
    fs->close = (void*)close;
    fs->readdir = (void*)readdir;
    fs->mkdir = (void*)mkdir;
    fs->unlink = (void*)unlink;
    fs->rename = (void*)rename;
    fs->private_data = NULL;

    int result = fs->mount(fs, device);
    if (result != 0)
    {
        printf("[VFS] Failed to mount '%s' on device %d\n", name, device);
        return -1;
    }

    printf("[VFS] Mounted '%s' on device %d\n", name, device);
    num_filesystems++;
    return 0;
}

int vfs_open(const char* path, int flags)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        int fd = filesystems[i].open(&filesystems[i], path, flags);
        if (fd >= 0)
        {
            int new_fd = -1;
            for (int j = 0; j < MAX_OPEN_FILES; j++)
            {
                if (!open_files[j].used)
                {
                    open_files[j].used = 1;
                    open_files[j].fs_id = i;
                    open_files[j].position = 0;
                    open_files[j].flags = flags;
                    strncpy(open_files[j].path, path, 255);
                    new_fd = j;
                    break;
                }
            }
            if (new_fd < 0) return -1;
            return new_fd;
        }
    }
    return -1;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used)
        return -1;

    int fs_id = open_files[fd].fs_id;
    filesystems[fs_id].close(&filesystems[fs_id], fd);

    open_files[fd].used = 0;
    return 0;
}

int vfs_read(int fd, void* buffer, uint32_t size)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used)
        return -1;

    int fs_id = open_files[fd].fs_id;
    return filesystems[fs_id].read(&filesystems[fs_id], open_files[fd].path, buffer, size);
}

int vfs_write(int fd, const void* buffer, uint32_t size)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used)
        return -1;

    int fs_id = open_files[fd].fs_id;
    return filesystems[fs_id].write(&filesystems[fs_id], open_files[fd].path, buffer, size);
}

int vfs_readdir(const char* path, char* entries, int max_entries)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        int result = filesystems[i].readdir(&filesystems[i], path, entries, max_entries);
        if (result >= 0) return result;
    }
    return -1;
}

int vfs_mkdir(const char* path)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        int result = filesystems[i].mkdir(&filesystems[i], path);
        if (result == 0) return 0;
    }
    return -1;
}

int vfs_unlink(const char* path)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        int result = filesystems[i].unlink(&filesystems[i], path);
        if (result == 0) return 0;
    }
    return -1;
}

int vfs_rename(const char* old_path, const char* new_path)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        if (filesystems[i].rename)
        {
            int result = filesystems[i].rename(&filesystems[i], old_path, new_path);
            if (result == 0) return 0;
        }
    }
    return -1;
}

int vfs_exists(const char* path)
{
    for (int i = 0; i < num_filesystems; i++)
    {
        int result = filesystems[i].open(&filesystems[i], path, 0);
        if (result >= 0) return 1;
    }
    return 0;
}
