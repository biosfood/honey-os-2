//
// Created by lukas on 4/15/25.
//
#include "kernelfs.h"

#include "process.h"

#include <stddef.h>
#include <vfs.h>

// ramfs is a basic implementation of the file system that stores all its data
// in the kernel memory. directories store their children in a tree-like
// structure.

File *kernelFsGet(KernelFsFileSystem *file_system, char *path) {
    if (stringEquals(path, "/")) {
        return (void *)&file_system->rootDir;
    }
    if (stringEquals(path, "/mem")) {
        return (void *)&file_system->mem;
    }
    if (stringEquals(path, "/null")) {
        return (void *)&file_system->null;
    }
    return NULL;
}

File *kernelFsCreate(File *directory, char *name, enum FileType type) {
    return NULL;
}

void kernelFsWrite(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset) {
    file->write(file, data, size, offset);
}

void kernelfs_read(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset, struct ProcessThread *thread,
                   struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
    } else {
        file->read(file, data, size, offset, thread, descriptor, bytes_read);
    }
}

FileSystemType kernelFsType = {
    .getFile = kernelFsGet,
    .create = kernelFsCreate,
    .write = kernelFsWrite,
    .read = kernelfs_read,
};

uint32_t null_write(KernelFsFile *file, void *data, uint32_t size,
                    uint32_t offset) {
    return size;
}

uint32_t null_read(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset, struct ProcessThread *thread,
                   struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    listAdd(&threads_to_process, thread);
    *bytes_read = 0;
}

KernelFsFileSystem kernel_fs_file_system = {
    .mountedInstances = NULL,
    .name = "KERNELFS",
    .type = &kernelFsType,
    .data = NULL,
    .rootDir =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_DIRECTORY,
            .data = NULL,
            .read = NULL,
            .write = NULL,
            .file_descriptors = NULL,
        },
    .mem =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .read = NULL,
            .write = NULL,
            .file_descriptors = NULL,
        },
    .null =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .read = null_read,
            .write = null_write,
            .file_descriptors = NULL,
        },
};

FileSystem *createKernelFs() { return (void *)&kernel_fs_file_system; }
