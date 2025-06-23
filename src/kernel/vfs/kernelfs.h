//
// Created by lukas on 4/15/25.
//

#ifndef KERNELFS_H
#define KERNELFS_H
#include <vfs.h>

typedef struct {
    File;
    void (*write)(struct KernelFsFile *file, void *data, uint32_t size, uint32_t offset);
    uint32_t (*read)(struct KernelFsFile *file, void *data, uint32_t size, uint32_t offset);
} KernelFsFile;

typedef struct {
    FileSystem;
    KernelFsFile rootDir;
    KernelFsFile mem;
    KernelFsFile null;
} KernelFsFileSystem;

#endif //KERNELFS_H
