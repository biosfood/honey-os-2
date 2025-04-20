#include <memory.h>
#include <process.h>
#include <stddef.h>
#include <sys/mman.h>
#include <syscall.h>
#include <util.h>

typedef struct {
    void *addr;
    size_t len;
    int prot;
    int flags;
    int filedes;
    uint32_t off;
} __attribute__((packed)) MmapArgs;
#define pageCount(x) ((((x) - 1) >> 12) + 1)

void handleMmapSyscall(ProcessThread *thread) {
    MmapArgs args;
    MmapArgs *threadArgs = mapTemporaryA(
        getPhysicalAddress(thread->process->memory_information.pageDirectory,
                           PTR(thread->parameters[0])));
    memcpy(threadArgs, &args, sizeof(MmapArgs));
    uint32_t pagesCount = pageCount(args.len);

    if (args.flags & MAP_ANON) {
        if (args.filedes != -1) {
            thread->returnValue = -1;
            thread->resume = true;
            return;
        }
        void *target = PTR(thread->parameters[1]);
        uint32_t virtualStart = PAGE_ID(target);
        if (!target) {
            virtualStart = findMultiplePages(
                &thread->process->memory_information, pagesCount);
        }
        reservePagesCount(&thread->process->memory_information, virtualStart,
                          pagesCount);

        for (uint32_t i = 0; i < pagesCount; i++) {
            uint32_t physicalPage = findPage(kernelPhysicalPages);
            reservePagesCount(kernelPhysicalPages, physicalPage, 1);
            mapPage(&thread->process->memory_information, ADDRESS(physicalPage),
                    ADDRESS(virtualStart + i), true, false);
        }
        thread->returnValue = U32(ADDRESS(virtualStart));
        thread->resume = true;
    }
}

void handleMunmapSyscall(const ProcessThread *thread) {
    const uint32_t address = thread->parameters[0];
    const uint32_t virtualPageId = PAGE_ID(address);
    giveUpPage(&thread->process->memory_information, virtualPageId);
}