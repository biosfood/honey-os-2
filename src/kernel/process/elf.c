#include <process.h>
#include "elf.h"

void build_starting_stack(ProcessThread *thread, void *start_position) {
    Process *process = thread->process;
    // build the initial stack, for starters just with a single page
    PhysicalMemoryEntry *physical = get_single_page_physical_memory_entry();
    // we will allocate a full 8MB for the virtual size of the stack, so we can
    // extend it any time.
    VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
    virtual->process = process;
    virtual->virtual = ADDRESS(
        findMultiplePages(&process->memory_information, 2048));
    virtual->type = MEM_TYPE_STACK;
    virtual->size = 4096 * 2048;
    reservePagesCount(&process->memory_information, PAGE_ID(virtual->virtual),
                      2048);

    MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
    mapping->physical = physical;
    physical->refcount++;
    mapping->virtual = ADDRESS(PAGE_ID(virtual->virtual) + 2047);
    mapping->copy_on_write = false;

    listAdd(&virtual->mappings, mapping);
    listAdd(&process->virtual_memory_entries, virtual);

    void *esp = mapTemporaryA(physical->physical);
    esp += 0x1000 - 0x10;
    *(void **)esp = start_position;
    *(void **)(esp + 0x4) = &runEnd;

    thread->esp = virtual->virtual + 4096 * 2048 - 0x10;
}
extern void *functionsStart;
extern void *functionsEnd;

PhysicalMemoryEntry kernel_functions_physical = {.physical = 0, 4, 0};


ProcessThread *processLoadELF(Process *process, File *file) {
    // use this function ONLY to load the initrd/loader program(maybe also the
    // ELF loader service)!
    struct stat s;
    file->file_system->type->getattr(file, &s);
    void *file_data = malloc(s.st_size);
    file->file_system->type->read(file, file_data, s.st_size, 0);

    ElfHeader *header = file_data;
    ProgramHeader *programHeader =
        file_data + header->programHeaderTablePosition;
    if (!kernel_functions_physical.physical) {
        kernel_functions_physical.physical =
            getPhysicalAddressKernel(&functionsStart);
    }
    process_map_memory_simple(process, &kernel_functions_physical,
                              &functionsStart)
        ->type = MEM_TYPE_KERNEL;

    // reserve first few pages to hopefully catch NULL pointers correctly
    reservePagesCount(&(process->memory_information), 0, 0x10);
    for (uint32_t i = 0; i < header->programHeaderEntryCount; i++) {
        VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
        virtual->virtual = PTR(programHeader->virtualAddress);
        virtual->process = process;
        virtual->type = MEM_TYPE_PROGRAM_DATA;
        virtual->mappings = NULL;
        virtual->size = programHeader->segmentMemorySize;
        listAdd(&process->virtual_memory_entries, virtual);

        for (uint32_t page = 0; page < programHeader->segmentMemorySize;
             page += 0x1000) {
            PhysicalMemoryEntry *physical_memory_entry =
                get_single_page_physical_memory_entry();
            if (programHeader->segmentFileSize > page) {
                void *mapped = mapTemporaryA(physical_memory_entry->physical);
                memcpy(file_data + programHeader->dataOffset + page, mapped,
                       MIN(0x1000, programHeader->segmentFileSize - page));
            }
            MAP(virtual, physical_memory_entry,
                PTR(programHeader->virtualAddress + page));
        }
    end:
        programHeader = (void *)programHeader + header->programHeaderEntrySize;
    }
    ProcessThread *thread = malloc(sizeof(ProcessThread));
    memset(thread, 0, sizeof(ProcessThread));
    thread->id = id_counter++;
    thread->process = process;
    listAdd(&process->threads, thread);
    thread->function = 0;
    build_starting_stack(thread, PTR(header->entryPosition));

    foreach (
        process->virtual_memory_entries, VirtualMemoryEntry *,
        virtual_memory_entry, {
            foreach (virtual_memory_entry->mappings, MemoryMapping *, mapping, {
                for (uint32_t i = 0; i < mapping->physical->page_count; i++) {
                    map(&process->memory_information,
                        mapping->physical->physical + 4096 * i,
                        mapping->virtual + 4096 * i, true)
                        ->writable = true;
                }
            })
                ;
        })
        ;
    listAdd(&threads_to_process, thread);
    free(file_data);
    return thread;
}
