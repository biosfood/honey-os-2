//
// Created by lukas on 4/15/25.
//

#ifndef KERNELFS_H
#define KERNELFS_H
#include <vfs.h>

typedef struct KernelFsFile {
    File;
    void (*write)(struct KernelFsFile *file, void *data, uint32_t size,
                  uint32_t offset);
    uint32_t (*read)(struct KernelFsFile *file, void *data, uint32_t size,
                     uint32_t offset, struct ProcessThread *thread,
                     struct FileDescriptor *descriptor, uint32_t *bytes_read);
} KernelFsFile;

typedef struct {
    uint32_t physical_base;
    uint32_t size;
} KernelFsMemRange;

typedef struct {
    FileSystem;
    KernelFsFile rootDir;
    KernelFsFile mem;
    KernelFsFile null;
} KernelFsFileSystem;

extern KernelFsFileSystem kernel_fs_file_system;

#endif // KERNELFS_H
