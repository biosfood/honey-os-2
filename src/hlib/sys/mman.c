//
// Created by lukas on 4/19/25.
//
#include <hlib.h>
#include <sys/mman.h>
#include <syscalls.h>

typedef struct {
    void *addr;
    size_t len;
    int prot;
    int flags;
    int filedes;
    off_t off;
} __attribute__((packed)) MmapArgs;

void *mmap(void *addr, const size_t len, const int prot, const int flags,
           const int fildes, const off_t off) {
    if (!len) {
        return NULL;
    }
    MmapArgs args = {addr, len, prot, flags, fildes, off};
    return PTR(syscall(SYS_MMAP, U32(&args), NULL, NULL, NULL));
}

int munmap(void *addr, size_t len) {
    return (int)syscall(SYS_MUNMAP, U32(addr), len, 0, 0);
}
