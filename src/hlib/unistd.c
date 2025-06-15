//
// Created by lukas on 3/27/25.
//
#include <hlib.h>
#include <syscalls.h>
#include <unistd.h>

ssize_t read(const int filedes, void *buffer, size_t nbyte) {
    return (int)syscall(SYS_READ, filedes, U32(buffer), nbyte, 0);
}

ssize_t pread(const int filedes, void *buffer, size_t nbyte, off_t offset) {
    return (int)syscall(SYS_READ, filedes, U32(buffer), nbyte, offset);
}

ssize_t write(const int filedes, void *buffer, size_t nbyte) {
    return (int)syscall(SYS_WRITE, filedes, U32(buffer), nbyte, 0);
}

ssize_t pwrite(const int filedes, void *buffer, size_t nbyte, off_t offset) {
    return (int)syscall(SYS_WRITE, filedes, U32(buffer), nbyte, offset);
}

int close(int fildes) { return (int)syscall(SYS_CLOSE, fildes, 0, 0, 0); }

int execv(const char *path, char *const argv[]) {
    return (int)syscall(SYS_EXEC, U32(path), 0, 0, 0);
}

pid_t fork() {
    void *ret_addr = __builtin_return_address(0);
    return (int)syscall(SYS_FORK, 0, 0, 0, U32(ret_addr));
}