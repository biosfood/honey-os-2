//
// Created by lukas on 4/19/25.
//

#ifndef MMAN_H
#define MMAN_H

#include <sys/types.h>

extern void *mmap(void *addr, size_t len, int prot, int flags, int fildes,
                  off_t off);
extern int munmap(void *addr, size_t len);

#define PROT_EXEC 1
#define PROT_NONE 2
#define PROT_READ 4
#define PROT_WRITE 8

#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FIXED 2
#define MAP_PRIVATE 4
#define MAP_SHARED 8

#endif // MMAN_H
