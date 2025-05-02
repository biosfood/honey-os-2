//
// Created by lukas on 4/19/25.
//

#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>

typedef struct AllocationBlock {
    uint8_t data[3948];
    uint32_t allocatedFine[32];
    uint32_t allocatedCoarse;
    uint32_t blockSize;
    struct AllocationBlock *next;
    struct AllocationBlock *previous;
    uint32_t magic;
} AllocationBlock;

extern AllocationBlock *allocationData[12];
typedef AllocationBlock **AllocationData;

extern void free(void *ptr);
extern void *malloc_(AllocationData, uint32_t);

static inline void *malloc(size_t size) {
    return malloc_(allocationData, size);
}

#endif // STDLIB_H
