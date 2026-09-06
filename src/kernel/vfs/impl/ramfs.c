//
// Created by lukas on 3/30/25.
//

#include "ramfs.h"

#include "process.h"

#include <util.h>

#include <dirent.h>
#include <stddef.h>
#include <vfs.h>

// ramfs is a basic implementation of the file system that stores all its data
// in the kernel memory. directories store their children in a tree-like
// structure.

FileOperationStatus ram_fs_get(FileSystem *file_system, char *path,
                               struct ProcessThread *thread, File **result,
                               void **scratchpad) {
    if (*path != '/') {
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    path++;
    if (!*path) {
        // root directory
        *result = file_system->data;
        return FILE_OPERATION_DONE;
    }
    RamFsFile *current_dir = file_system->data;
    while (*path) {
        uint8_t next_slash = 0;
        while (path[next_slash] != '/' && path[next_slash]) {
            next_slash++;
        }
        char old = path[next_slash];
        path[next_slash] = 0;
        RamFsFile *found_file = NULL;
        foreach (current_dir->data, RamFsFile *, file, {
            if (stringEquals(file->name, path)) {
                found_file = file;
                break;
            }
        })
            ;
        path[next_slash] = old;
        if (!found_file) {
            *result = NULL;
            return FILE_OPERATION_DONE;
        }
        path += next_slash;
        if (!*path || *path == '/' && !path[1]) {
            *result = (void*)found_file;
            return FILE_OPERATION_DONE;
        }
        if (found_file->type != FILE_TYPE_DIRECTORY) {
            *result = NULL;
            return FILE_OPERATION_DONE;
        }
        path++;
        current_dir = found_file;
    }
    return FILE_OPERATION_WILL_SCHEDULE;
}

int ramFsCreate(File *directory, char *name, enum FileType type) {
    if (directory->type != FILE_TYPE_DIRECTORY) {
        return -1;
    }
    foreach (directory->data, RamFsFile *, file, {
        if (stringEquals(file->name, name)) {
            return -1;
        }
    })
        ;
    RamFsFile *file = malloc(sizeof(RamFsFile));
    file->name = combineStrings(name, "");
    file->size = 0;
    file->data = NULL;
    file->file_system = directory->file_system;
    file->type = type;
    listAdd((void *)&directory->data, file);
    return 0;
}

void ramfs_write(RamFsFile *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_written) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_write((File *)file, data, size, bytes_written, thread);
        return;
    }
    if (file->type == FILE_TYPE_DIRECTORY) {
        if (thread) {
            listAdd(&threads_to_process, thread);
        }
        return;
    }
    if (offset + size > file->size) {
        void *new_data = malloc(offset + size);
        if (file->data) {
            if (offset > 0) {
                memcpy(file->data, new_data, MIN(file->size, offset));
            }
            if (offset > file->size) {
                memset(new_data + file->size, 0, offset - file->size);
            }
            free(file->data);
        }
        file->data = new_data;
        file->size = offset + size;
    }
    memcpy(data, file->data + offset, size);
    *bytes_written = size;
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
}

void fill_dirent(FillDirData *buf, char *name, int file_type) {
    uint32_t entry_size = sizeof(struct posix_dent) + strlen(name) + 1;
    if (buf->current_offset + entry_size < buf->offset) {
        buf->current_offset += entry_size;
        return;
    }
    if (buf->current_offset > buf->offset + buf->size) {
        return;
    }
    struct posix_dent *dirent = malloc(entry_size);
    dirent->d_ino =
        U32(name); // just needs to be unique, and in ramfs, the address of the
                   // filename is unique for each file...
    dirent->d_off = buf->current_offset + entry_size;
    memcpy(name, dirent->d_name, strlen(name) + 1);
    dirent->d_reclen = entry_size;
    dirent->d_type = file_type;
    uint32_t copy_len =
        MIN(MAX(MIN((buf->offset + buf->size) - buf->current_offset,
                    (buf->current_offset + entry_size) - buf->offset),
                0),
            entry_size);
    memcpy(dirent + MAX((int32_t)(buf->offset - buf->current_offset), 0),
           buf->data + MAX(buf->current_offset - buf->offset, 0), copy_len);
    free(dirent);
    buf->current_offset += copy_len;
    buf->bytes_written += copy_len;
}

void ramfs_read(RamFsFile *file, void *data, uint32_t size, uint32_t offset,
                ProcessThread *thread, FileDescriptor *descriptor,
                uint32_t *bytes_read) {
    if (file->type == FILE_TYPE_FILE || file->type == FILE_TYPE_SYMLINK) {
        uint32_t bytes_to_read =
            MAX(0, MIN((int32_t)size, (int32_t)(file->size - offset)));
        if (!bytes_to_read) {
            *bytes_read = 0;
        } else {
            memcpy(file->data + offset, data, bytes_to_read);
            *bytes_read = bytes_to_read;
        }
        if (thread) {
            listAdd(&threads_to_process, thread);
        }
    }
    if (file->type == FILE_TYPE_DIRECTORY) {
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        foreach (file->data, RamFsFile *, child,
                 { fill_dirent(&buf, child->name, child->type); })
            ;
        *bytes_read = buf.bytes_written;
        if (thread) {
            listAdd(&threads_to_process, thread);
        }
    }
    if (file->type == FILE_TYPE_FIFO) {
        fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
    }
}

uint32_t ramFsGetattr(RamFsFile *file, struct stat *buf) {
    memset(buf, 0, sizeof(struct stat));
    if (file->type == FILE_TYPE_DIRECTORY) {
        uint32_t size = 0;
        foreach (file->data, RamFsFile *, child,
                 { size += sizeof(struct posix_dent) + strlen(child->name) + 1; })
            ;
        buf->st_size = size;
        buf->st_mode = S_IFDIR | 0755;
    } else if (file->type == FILE_TYPE_FIFO) {
        buf->st_size = 0;
        buf->st_mode = S_IFIFO | 0666;
    } else if (file->type == FILE_TYPE_SYMLINK) {
        buf->st_size = file->size;
        buf->st_mode = S_IFLNK | 0777;
    } else {
        buf->st_size = file->size;
        buf->st_mode = S_IFREG | 0644;
    }
    buf->st_ino = U32(file->data ? file->data : file);
    buf->st_nlink = 1;
    buf->st_blksize = 4096;
    buf->st_blocks = (buf->st_size + 511) / 512;
    return 0;
}

FileSystemType ramfsType = {
    .getFile = (void*)ram_fs_get,
    .create = (void*)ramFsCreate,
    .write = (void*)ramfs_write,
    .read = (void*)ramfs_read,
    .getattr = (void*)ramFsGetattr,
};

FileSystem *createRamfs() {
    RamFsFile *rootDir = malloc(sizeof(RamFsFile));
    rootDir->data = NULL;
    rootDir->type = FILE_TYPE_DIRECTORY;
    rootDir->name = "/";
    rootDir->size = 0;
    rootDir->file_descriptors = NULL;
    FileSystem *file_system = malloc(sizeof(FileSystem));
    rootDir->file_system = file_system;
    file_system->data = rootDir;
    file_system->name = "RAMFS";
    file_system->type = &ramfsType;
    file_system->mountedInstances = NULL;
    return file_system;
}
