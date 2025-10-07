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
    MmapArgs *threadArgs = copy_from_process_to_kernel(thread->process, PTR(thread->parameters[0]), sizeof(MmapArgs));
    uint32_t pagesCount = pageCount(threadArgs->len);

    if (threadArgs->flags & MAP_ANON) {
        if (threadArgs->filedes != -1) {
            thread->returnValue = -1;
            listAdd(&threads_to_process, thread);
            return;
        }
        void *target = threadArgs->addr;
        uint32_t virtualStart = PAGE_ID(target);
        if (!target) {
            virtualStart = findMultiplePages(
                &thread->process->memory_information, pagesCount);
        }
        reservePagesCount(&thread->process->memory_information, virtualStart,
                          pagesCount);
        VirtualMemoryEntry *virtual_memory_entry =
            malloc(sizeof(VirtualMemoryEntry));
        virtual_memory_entry->virtual = ADDRESS(virtualStart);
        virtual_memory_entry->process = thread->process;
        virtual_memory_entry->type = MEM_TYPE_HEAP;
        virtual_memory_entry->mappings = NULL;
        virtual_memory_entry->size = pagesCount * 4096;
        listAdd(&thread->process->virtual_memory_entries, virtual_memory_entry);

        for (uint32_t i = 0; i < pagesCount; i++) {
            PhysicalMemoryEntry *physical =
                get_single_page_physical_memory_entry();
            MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
            mapping->physical = physical;
            physical->refcount++;
            mapping->virtual = ADDRESS(virtualStart + i);
            mapping->copy_on_write = false;
            map(&thread->process->memory_information, physical->physical,
                mapping->virtual, true)
                ->writable = true;
            listAdd(&virtual_memory_entry->mappings, mapping);
        }
        thread->returnValue = U32(ADDRESS(virtualStart));
        listAdd(&threads_to_process, thread);
    }
}

void handleMunmapSyscall(const ProcessThread *thread) {
    const uint32_t address = thread->parameters[0];
    const uint32_t virtualPageId = PAGE_ID(address);
    giveUpPage(&thread->process->memory_information, virtualPageId);
    listAdd(&threads_to_process, thread);
}