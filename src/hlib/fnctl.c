//
// Created by lukas on 3/27/25.
//
#include <fnctl.h>
#include <hlib.h>
#include <syscalls.h>

int open(const char *path, const int oflag, ...) {
    return (int) syscall(SYS_OPEN, U32(path), oflag, 0,0);
}

