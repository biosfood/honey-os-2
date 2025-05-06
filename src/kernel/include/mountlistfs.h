//
// Created by lukas on 5/3/25.
//

#ifndef MOUNTLISTFS_H
#define MOUNTLISTFS_H

#include <vfs.h>

extern FileSystem *create_mount_list_file_system();
extern void mount(FileSystem *mount_list_file_system, FileSystem *file_system,
                  char *mountPoint, char *pathOffset);

#endif // MOUNTLISTFS_H
