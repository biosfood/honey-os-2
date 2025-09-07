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
#include <string.h>
#include <sys/stat.h>
#include <syscalls.h>

uint32_t get_last_slash_position(char *string) {
    uint32_t result = strlen(string);
    if (!result) {
        return 0;
    }
    result--;
    while (result && string[result] != '/') {
        result--;
    }
    return result;
}

int create(char *path, int type) {
    uint32_t last_slash_position = get_last_slash_position(path);
    if (last_slash_position == 0) {
        int fd = open("/", 0);
        if (fd < 0) {
            return -1;
        }
        int result = (int)syscall(SYS_FILE_CREATE, fd, U32(path + 1), type, 0);
        close(fd);
        return result;
    }
    path[last_slash_position] = 0;
    int fd = open(path, 0);
    path[last_slash_position] = '/';
    if (fd < 0) {
        return -1;
    }
    int result = (int)syscall(SYS_FILE_CREATE, fd, U32(path + last_slash_position + 1), type, 0);
    close(fd);
    return result;
}

int mkfifo(const char *path, mode_t mode) {
    return create(path, FILE_TYPE_FIFO);
}

int mkdir(const char *path, mode_t mode) {
    return create(path, FILE_TYPE_DIRECTORY);
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