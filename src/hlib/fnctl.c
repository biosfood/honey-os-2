//
// Created by lukas on 3/27/25.
//
#include <fnctl.h>
#include <hlib.h>
#include <syscalls.h>

// same as in kernel
enum FileType {
    FILE_TYPE_DIRECTORY = 0,
    FILE_TYPE_SYMLINK = 1,
    FILE_TYPE_FILE = 2,
    FILE_TYPE_FIFO = 3,
    FILE_TYPE_SOCKET = 4,
    FILE_TYPE_LINK = 5,
};

extern int create(char *path, int type);

int open(const char *path, const int oflag, ...) {
    int retval = (int)syscall(SYS_OPEN, U32(path), oflag, 0, 0);
    if (retval > 0 || !(oflag & O_CREAT)) {
        return retval;
    }
    retval = create(path, FILE_TYPE_FILE);
    if (retval < 0) {
        return retval;
    }
    return (int)syscall(SYS_OPEN, U32(path), oflag, 0, 0);
}
