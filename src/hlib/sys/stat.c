//
// Created by lukas on 3/27/25.
//

#include <fnctl.h>

// same as in kernel
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

int fstat(int fildes, struct stat *buf) {
    return (int)syscall(SYS_STAT, fildes, U32(buf), 0, 0);
}

int stat(const char *restrict path, struct stat *restrict buf) {
    int open_result = open(path, 0);
    if (open_result < 0) {
        return open_result;
    }
    return fstat(open_result, buf);
}