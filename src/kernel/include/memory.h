#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stdint.h>
#include "../kernel/memory/malloc.h"

#define ADDRESS(pageId) PTR((pageId) << 12)
#define PAGE_ID(address) (U32(address) >> 12)
#define PAGE_OFFSET(address) (U32(address) & 0xFFF)

typedef struct {
    uint32_t pageOffset : 12;
    uint32_t pageTableIndex : 10;
    uint32_t pageDirectoryIndex : 10;
} __attribute__((packed)) VirtualAddress;

typedef struct {
    uint32_t present : 1;
    uint32_t writable : 1;
    uint32_t belongsToUserProcess : 1;
    uint32_t reserved : 2;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t reserved2 : 2;
    uint32_t available : 3;
    uint32_t pageTableID : 20;
} __attribute__((packed)) PageDirectoryEntry;

typedef struct {
    PageDirectoryEntry *pageDirectory;
    uint32_t isPageTableInUse[0x20];
    uint32_t isPageAllocated[0x8000];
    uint32_t isPageConnectedToNext[0x8000];
    uint32_t isPageAllocatedCoarse[0x400];
    uint32_t pageSearchStart;
} PagingInfo;

typedef struct {
    uint32_t present : 1;
    uint32_t writable : 1;
    uint32_t belongsToUserProcess : 1;
    uint32_t writeThrough : 1;
    uint32_t isVolatile : 1; // disables the cpu cache
    uint32_t accessed : 1;
    uint32_t reserved : 1;
    uint32_t is4MBPage : 1;
    uint32_t global : 1;
    uint32_t available : 3;
    uint32_t targetAddress : 20;
} __attribute__((packed)) PageTableEntry;

extern PagingInfo *kernelPhysicalPages;

extern void reservePagesUntilPhysical(uint32_t endPageId);

extern void *findTarFile(void *fileStart, uint32_t fileSize, char *filename);

extern void *kernelMapPhysical(void *address);
extern void *kernelMapPhysicalCount(void *address, uint32_t size);
extern void *getPage();
extern void *getPhysicalPage();
extern void *getPhysicalPages(uint32_t count);
extern void *sharePage(PagingInfo *destination, void *sourceAddress,
                       void *destinationAddress);
extern void freePage(void *pageAddress);
extern void freePhysicalPage(uint32_t pageId);
extern void unmapPage(void *pageAddress);
extern void unmapPageFrom(PagingInfo *info, void *address);
extern void giveUpPage(PagingInfo *info, uint32_t pageId);

extern void free(void *address);

extern void *getPhysicalAddressKernel(void *address);
extern void *getPhysicalAddress(PageDirectoryEntry *pageDirectory,
                                void *address);
extern uint32_t findPage(PagingInfo *info);
extern void *mapTemporaryA(void *address);
extern void *mapTemporaryB(void *address);
extern void *getPagesCount(uint32_t size);

extern uint32_t findMultiplePages(PagingInfo *info, uint32_t size);
extern void reservePagesCount(PagingInfo *info, uint32_t startPageId,
                              uint32_t count);
extern void mapPage(PagingInfo *info, void *physical, void *virtual,
                    bool userPage, bool isVolatile);
extern PageTableEntry *map(PagingInfo *info, void *physical, void *virtual, bool user);
extern void freePageFrom(PagingInfo *info, void *address);
struct PhysicalMemoryEntry;
extern struct PhysicalMemoryEntry *get_single_page_physical_memory_entry();
extern void freePhysical(void *address, uint32_t count);

#endif
