#include <memory.h>
#include <process.h>
#include <stddef.h>
#include <sys/mman.h>
#include <syscall.h>
#include <util.h>
#include "../vfs/impl/kernelfs.h"

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
    if (!threadArgs || threadArgs->len == 0) {
        thread->returnValue = -1;
        listAdd(&threads_to_process, thread);
        return;
    }
    uint32_t pagesCount = pageCount(threadArgs->len);

    if (threadArgs->filedes == -1 && (threadArgs->flags & MAP_ANON)) {
        bool isShared = (threadArgs->flags & MAP_SHARED);
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
        virtual_memory_entry->type = isShared ? MEM_TYPE_SHARED : MEM_TYPE_HEAP;
        virtual_memory_entry->mappings = NULL;
        virtual_memory_entry->size = pagesCount * 4096;
        listAdd(&thread->process->virtual_memory_entries, virtual_memory_entry);

        uint32_t physContigStart = 0;
        if (isShared) {
            physContigStart = findMultiplePages(kernelPhysicalPages, pagesCount);
            reservePagesCount(kernelPhysicalPages, physContigStart, pagesCount);
        }

        for (uint32_t i = 0; i < pagesCount; i++) {
            PhysicalMemoryEntry *physical;
            if (isShared) {
                physical = malloc(sizeof(PhysicalMemoryEntry));
                physical->physical = ADDRESS(physContigStart + i);
                physical->page_count = 1;
                physical->refcount = 1;
            } else {
                physical = get_single_page_physical_memory_entry();
                physical->refcount++;
            }
            void * data = mapTemporaryA(physical->physical);
            memset(data, 0, 0x1000);
            MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
            mapping->physical = physical;
            mapping->virtual = ADDRESS(virtualStart + i);
            mapping->copy_on_write = false;
            map(&thread->process->memory_information, physical->physical,
                mapping->virtual, true)
                ->writable = true;
            listAdd(&virtual_memory_entry->mappings, mapping);
        }
        thread->returnValue = U32(ADDRESS(virtualStart));
        listAdd(&threads_to_process, thread);
        return;
    }

    if (threadArgs->filedes >= 0) {
        FileDescriptor *desc = NULL;
        foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
            if (descriptor->id == threadArgs->filedes) {
                desc = descriptor;
                break;
            }
        });

        if (!desc || !desc->file || desc->file->file_system != (void *)&kernel_fs_file_system) {
            thread->returnValue = -1;
            listAdd(&threads_to_process, thread);
            return;
        }

        uint32_t physStart = 0;
        if (desc->file == (void *)&kernel_fs_file_system.mem) {
            if (threadArgs->off & 0xFFF) {
                thread->returnValue = -1;
                listAdd(&threads_to_process, thread);
                return;
            }
            physStart = threadArgs->off;
        } else if (desc->file->data != NULL && desc->file->data != (void *)1) {
            KernelFsMemRange *range = (KernelFsMemRange *)desc->file->data;
            if (threadArgs->off & 0xFFF) {
                thread->returnValue = -1;
                listAdd(&threads_to_process, thread);
                return;
            }
            if (threadArgs->off + threadArgs->len > range->size) {
                thread->returnValue = -1;
                listAdd(&threads_to_process, thread);
                return;
            }
            physStart = range->physical_base + threadArgs->off;
        } else {
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
        reservePagesCount(&thread->process->memory_information, virtualStart, pagesCount);

        VirtualMemoryEntry *virtual_memory_entry = malloc(sizeof(VirtualMemoryEntry));
        virtual_memory_entry->virtual = ADDRESS(virtualStart);
        virtual_memory_entry->process = thread->process;
        virtual_memory_entry->type = MEM_TYPE_MMIO;
        virtual_memory_entry->mappings = NULL;
        virtual_memory_entry->size = pagesCount * 4096;
        listAdd(&thread->process->virtual_memory_entries, virtual_memory_entry);

        for (uint32_t i = 0; i < pagesCount; i++) {
            PhysicalMemoryEntry *physical = malloc(sizeof(PhysicalMemoryEntry));
            physical->physical = ADDRESS(PAGE_ID(physStart) + i);
            physical->page_count = 1;
            physical->refcount = 1;

            MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
            mapping->physical = physical;
            mapping->virtual = ADDRESS(virtualStart + i);
            mapping->copy_on_write = false;

            mapPage(&thread->process->memory_information,
                    physical->physical,
                    mapping->virtual,
                    /*userPage=*/true,
                    /*isVolatile=*/true);

            listAdd(&virtual_memory_entry->mappings, mapping);
        }

        thread->returnValue = U32(ADDRESS(virtualStart));
        listAdd(&threads_to_process, thread);
        return;
    }

    thread->returnValue = -1;
    listAdd(&threads_to_process, thread);
}

void handleMunmapSyscall(const ProcessThread *thread) {
    const uint32_t address = thread->parameters[0];
    const uint32_t virtualPageId = PAGE_ID(address);
    giveUpPage(&thread->process->memory_information, virtualPageId);
    listAdd(&threads_to_process, thread);
}