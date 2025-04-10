//
// Created by lukas on 3/31/25.
//

#ifndef RAMFS_H
#define RAMFS_H
#include <vfs.h>

typedef struct {
    File;
    char *name;
} RamFsFile;

#endif // RAMFS_H
