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

File *ramFsGet(FileSystem *file_system, char *path) {
    if (*path != '/')
        return NULL;
    path++;
    if (!*path) {
        return file_system->data;
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
            return NULL;
        }
        path += next_slash;
        if (!*path || *path == '/' && !path[1]) {
            return (void *)found_file;
        }
        if (found_file->type != FILE_TYPE_DIRECTORY) {
            return NULL;
        }
        path++;
        current_dir = found_file;
    }
}

File *ramFsCreate(File *directory, char *name, enum FileType type) {
    if (directory->type != FILE_TYPE_DIRECTORY) {
        return NULL;
    }
    foreach (directory->data, RamFsFile *, file, {
        if (stringEquals(file->name, name)) {
            return NULL;
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
    return (void *)file;
}

void ramfs_write(RamFsFile *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_written) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_write((File *)file, data, size, bytes_written, thread);
        return;
    }
    // just completely overwrites the file for now...
    if (file->data) {
        free(file->data);
    }
    file->data = malloc(size + offset);
    file->size = size + offset;
    memcpy(data, file->data + offset, size);
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
}

void fill_dirent(FillDirData *buf, char *name, int file_type) {
    uint32_t entry_size = sizeof(posix_dirent) + strlen(name) + 1;
    if (buf->current_offset + entry_size < buf->offset) {
        buf->current_offset += entry_size;
        return;
    }
    if (buf->current_offset > buf->offset + buf->size) {
        return;
    }
    posix_dirent *dirent = malloc(entry_size);
    dirent->d_ino =
        U32(name); // just needs to be unique, and in ramfs, the address of the
                   // filename is unique for each file...
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
                struct ProcessThread *thread, struct FileDescriptor *descriptor,
                uint32_t *bytes_read) {
    if (file->type == FILE_TYPE_FILE) {
        uint32_t bytes_to_read =
            MAX(0, MIN((int32_t)size, (int32_t)(file->size - offset)));
        if (!bytes_to_read) {
            *bytes_read = 0;
        } else {
            memcpy(file->data + offset, data, size);
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
                 { fill_dirent(&buf, child->name, file->type); })
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
    if (file->type == FILE_TYPE_DIRECTORY) {
        uint32_t size = 0;
        foreach (file->data, RamFsFile *, child,
                 { size += sizeof(posix_dirent) + strlen(child->name) + 1; })
            ;
        buf->st_size = size;
    } else {
        buf->st_size = file->size;
    }
    buf->st_ino = U32(file->data);
}

FileSystemType ramfsType = {
    .getFile = ramFsGet,
    .create = ramFsCreate,
    .write = ramfs_write,
    .read = ramfs_read,
    .getattr = ramFsGetattr,
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
