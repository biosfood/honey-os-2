//
// Created by lukas on 3/27/25.
//

#include <syscalls.h>
#include <sys/stat.h>
#include <hlib.h>

int mkfifo(const char *path, mode_t mode) {
    return (int) syscall(SYS_MKFIFO, U32(path), mode, 0, 0);
}
