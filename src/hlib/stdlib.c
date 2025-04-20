#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define LOG2(X) ((unsigned)(64 - __builtin_clzll((X)) - 1))
#define ALLOCATION_MAGIC 0xB105F00D // == biosfood


void *reserveBlock(AllocationBlock *block, uint8_t coarse, uint8_t fine) {
    block->allocatedFine[coarse] |= 1 << fine;
    block->allocatedCoarse |= 1 << coarse;
    for (uint8_t i = 0; i < 32; i++) {
        if (block->allocatedFine[coarse] & 1 << i) {
            continue;
        }
        block->allocatedCoarse &= ~(1 << coarse);
        break;
    }
    void *result =
        ((uint8_t *)block) + block->blockSize * (32 * (uint32_t)coarse + fine);
    memset(result, 0, block->blockSize);
    return result;
}

void *malloc_(AllocationBlock *allocationData[12], uint32_t size) {
    const uint32_t sizeBit = LOG2(size) + 1;
    if (sizeBit > 10) {
        const uint32_t pages = ((size - 1) >> 12) + 1;
        return mmap(NULL, 4096 * pages, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
    }
    AllocationBlock *block = allocationData[sizeBit], *last = 0;
    while (1) {
        if (!block) {
            block = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
            memset(block, 0, 4096);
            block->blockSize = 1 << sizeBit;
            if (last) {
                block->previous = last;
                last->next = block;
            } else {
                allocationData[sizeBit] = block;
                block->previous = NULL;
            }
            block->magic = ALLOCATION_MAGIC;
        }
        if (block->allocatedCoarse == ~0) {
            goto end;
        }
        bool abort = false;
        for (uint8_t coarse = 0; coarse < 32; coarse++) {
            for (uint8_t fine = 0; fine < 32; fine++) {
                if (block->blockSize * (32 * coarse + fine + 1) > 3948) {
                    abort = true;
                    break;
                }
                if (block->allocatedFine[coarse] & (1 << fine)) {
                    continue;
                }
                return reserveBlock(block, coarse, fine);
            }
            if (abort) {
                break;
            }
        }
    end:
        last = block;
        block = block->next;
    }
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }
    AllocationBlock *block = (void *)((uintptr_t)ptr & ~0xFFF);
    if (block->magic != ALLOCATION_MAGIC) {
        munmap(ptr, 0);
        return;
    }
    uint16_t index = (uint16_t)((uintptr_t)ptr & 0xFFF) / block->blockSize;
    uint8_t coarse = index / 32;
    uint8_t fine = index % 32;
    block->allocatedFine[coarse] &= ~(1 << fine);
    block->allocatedCoarse &= ~(1 << coarse);
    // TODO: give up the block if it is completely free
}
