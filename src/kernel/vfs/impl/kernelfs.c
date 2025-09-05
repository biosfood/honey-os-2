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

FileOperationStatus kernelfs_get(KernelFsFileSystem *file_system, char *path,
                  ProcessThread *thread, File **result, void **scratchpad) {
    if (stringEquals(path, "/")) {
        *result = (void *)&file_system->rootDir;
    } else if (stringEquals(path, "/mem")) {
        *result = (void *)&file_system->mem;
    } else if (stringEquals(path, "/null")) {
        *result = (void *)&file_system->null;
    } else {
        *result = NULL;
    }
    return FILE_OPERATION_DONE;
}

File *kernelFsCreate(File *directory, char *name, enum FileType type) {
    return NULL;
}

void kernelfs_write(KernelFsFile *file, void *data, uint32_t size,
                    uint32_t offset, struct ProcessThread *thread,
                    struct FileDescriptor *descriptor,
                    uint32_t *bytes_written) {
    file->write(file, data, size, offset);
    *bytes_written = 0;
    listAdd(&threads_to_process, thread);
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
    .getFile = kernelfs_get,
    .create = kernelFsCreate,
    .write = kernelfs_write,
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
