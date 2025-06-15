#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

typedef struct {
    uint32_t pageOffset : 12;
    uint32_t pageTableIndex : 10;
    uint32_t pageDirectoryIndex : 10;
} __attribute__((packed)) VirtualAddress;

#endif
