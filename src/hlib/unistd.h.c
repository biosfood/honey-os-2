//
// Created by lukas on 3/27/25.
//
#include <hlib.h>
#include <unistd.h>
#include <syscalls.h>

ssize_t read(const int filedes, void *buffer, size_t nbyte) {
    return (int) syscall(SYS_READ, filedes, U32(buffer), nbyte,0);
}

ssize_t write(const int filedes, void *buffer, size_t nbyte) {
    return (int) syscall(SYS_WRITE, filedes, U32(buffer), nbyte,0);
}
