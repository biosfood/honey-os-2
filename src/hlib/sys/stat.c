//
// Created by lukas on 3/27/25.
//

enum FileType {
    FILE_TYPE_DIRECTORY = 0,
    FILE_TYPE_SYMLINK = 1,
    FILE_TYPE_FILE = 2,
    FILE_TYPE_FIFO = 3,
    FILE_TYPE_SOCKET = 4,
    FILE_TYPE_LINK = 5,
};

#include <hlib.h>
#include <sys/stat.h>
#include <syscalls.h>

int mkfifo(const char *path, mode_t mode) {
    return (int)syscall(SYS_FILE_CREATE, U32(path), FILE_TYPE_FIFO, 0, 0);
}

int mkdir(const char *path, mode_t mode) {
    return (int)syscall(SYS_FILE_CREATE, U32(path), FILE_TYPE_DIRECTORY, 0, 0);
}