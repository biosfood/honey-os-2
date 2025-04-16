//
// Created by lukas on 4/15/25.
//
#include "kernelfs.h"

#include <stddef.h>
#include <vfs.h>

// ramfs is a basic implementation of the file system that stores all its data
// in the kernel memory. directories store their children in a tree-like
// structure.

File *kernelFsGet(KernelFsFileSystem *file_system, char *path) {
    if (stringEquals(path, "/")) {
        return &file_system->rootDir;
    }
    if (stringEquals(path, "/mem")) {
        return &file_system->mem;
    }
    if (stringEquals(path, "/port")) {
        return &file_system->port;
    }
    if (stringEquals(path, "/cpuid")) {
        return &file_system->cpuid;
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

uint32_t kernelFsRead(KernelFsFile *file, void *data, uint32_t size,
                      uint32_t offset) {
    return file->read(file, data, size, offset);
}

FileSystemType kernelFsType = {
    .getFile = kernelFsGet,
    .create = kernelFsCreate,
    .write = kernelFsWrite,
    .read = kernelFsRead,
};

uint32_t cpuidRead(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset) {
    uint32_t output[4];
    asm volatile("cpuid"
                 : "=a"(output[0]), "=b"(output[1]), "=c"(output[2]),
                   "=d"(output[3])
                 : "a"(offset));
    memcpy(output, data, MIN(size, 16));
    return MIN(size, 16);
}

KernelFsFileSystem kernel_fs_file_system = {
    .mountedInstances = NULL,
    .name = "KERNELFS",
    .type = &kernelFsType,
    .data = NULL,
    .rootDir =
        {
            .file_system = &kernel_fs_file_system,
            .size = 0,
            .type = FILE_TYPE_DIRECTORY,
            .data = NULL,
            .data_ = NULL,
            .livesInMemory = true,
            .read = NULL,
            .write = NULL,
        },
    .mem =
        {
            .file_system = &kernel_fs_file_system,
            .size = 0,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .data_ = NULL,
            .read = NULL,
            .write = NULL,
            .livesInMemory = true,
        },
    .cpuid =
        {
            .file_system = &kernel_fs_file_system,
            .size = 0,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .data_ = NULL,
            .read = cpuidRead,
            .write = NULL,
            .livesInMemory = true,
        },
    .port =
        {
            .file_system = &kernel_fs_file_system,
            .size = 0,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .data_ = NULL,
            .read = NULL,
            .write = NULL,
            .livesInMemory = true,
        },
};

FileSystem *createKernelFs() { return &kernel_fs_file_system; }
