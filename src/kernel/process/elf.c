#include "elf.h"
#include <process.h>
#include <stddef.h>

// You'll need to define the AT_* values. They are standard in <elf.h>.
#define AT_NULL 0  /* End of vector */
#define AT_PHDR 3  /* Phdr base address */
#define AT_PHNUM 5 /* Number of phdrs */
#define AT_PHENT 4 /* Size of one phdr */

void build_starting_stack(ProcessThread *thread, void *start_position,
                          uintptr_t phdr_vaddr, int phnum, int phentsize) {
    Process *process = thread->process;
    // build the initial stack, for starters just with a single page
    PhysicalMemoryEntry *physical = get_single_page_physical_memory_entry();
    // we will allocate a full 8MB for the virtual size of the stack, so we can
    // extend it any time.
    VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
    virtual->process = process;
    virtual->virtual =
        ADDRESS(findMultiplePages(&process->memory_information, 2048));
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

    char *stack_ptr = (char *)mapTemporaryA(physical->physical) + 4096;

    // first argument to argv: program name, stored on the stack
    const char *prog_name = "/init";
    uint32_t name_len = strlen(prog_name) + 1;
    stack_ptr -= name_len;
    memcpy(prog_name, stack_ptr, name_len);
    char *user_prog_name_ptr =
        (char *)(virtual->virtual + virtual->size - name_len);

    stack_ptr = (char *)((uintptr_t)stack_ptr & -16L);

    // Push the Auxiliary Vector (auxv).
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = 0;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = AT_NULL;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = phentsize;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = AT_PHENT;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = phnum;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = AT_PHNUM;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = phdr_vaddr;
    stack_ptr -= sizeof(uintptr_t);
    *(uintptr_t *)stack_ptr = AT_PHDR;

    // envp (NULL).
    stack_ptr -= sizeof(char *);
    *(char **)stack_ptr = NULL;

    // Push argv.
    stack_ptr -= sizeof(char *);
    *(char **)stack_ptr = NULL; // argv[1]
    stack_ptr -= sizeof(char *);
    *(char **)stack_ptr = user_prog_name_ptr; // argv[0]

    // Push argc.
    stack_ptr -= sizeof(int);
    *(int *)stack_ptr = 1;

    // start position: kernel will RET as the last step for startup
    stack_ptr -= sizeof(void *);
    *(void **)stack_ptr = start_position;

    uintptr_t final_offset =
        ((char *)mapTemporaryA(physical->physical) + 4096) - stack_ptr;
    thread->esp = virtual->virtual + virtual->size - final_offset;
}
extern void *functionsStart;
extern void *functionsEnd;

PhysicalMemoryEntry kernel_functions_physical = {.physical = 0, 4, 0};

ProcessThread *processLoadELF(Process *process, void *file_data) {
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
    uintptr_t phdr_virtual = 0;
    for (uint32_t i = 0; i < header->programHeaderEntryCount; i++) {
        if (programHeader->segmentType != 1) {
            programHeader = (void *)programHeader + header->programHeaderEntrySize;
            continue;
        }
        if ((programHeader->segmentType == 6 ||
             programHeader->dataOffset == 0) &&
            !phdr_virtual) {
            phdr_virtual = programHeader->virtualAddress +
                           header->programHeaderTablePosition;
        }
        if (programHeader->segmentMemorySize == 0) {
            continue;
        }
        uintptr_t vaddr = programHeader->virtualAddress;
        uintptr_t memsz = programHeader->segmentMemorySize;
        uintptr_t filesz = programHeader->segmentFileSize;
        uintptr_t offset = programHeader->dataOffset;

        uintptr_t start_page = vaddr & ~0xFFF;
        uintptr_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
        uint32_t num_pages = (end_page - start_page) >> 12;

        VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
        virtual->virtual = ADDRESS(PAGE_ID(PTR(start_page)));
        virtual->process = process;
        virtual->type = MEM_TYPE_PROGRAM_DATA;
        virtual->mappings = NULL;
        virtual->size = num_pages * 0x1000; // Size spans all allocated pages
        listAdd(&process->virtual_memory_entries, virtual);

        for (uint32_t i = 0; i < num_pages; i++) {
            uintptr_t current_page_vaddr = start_page + (i * 0x1000);

            PhysicalMemoryEntry *physical_memory_entry =
                get_single_page_physical_memory_entry();
            void *mapped = mapTemporaryA(physical_memory_entry->physical);

            memset(mapped, 0, 0x1000);

            uintptr_t page_data_start = MAX(current_page_vaddr, vaddr);
            uintptr_t page_data_end =
                MIN(current_page_vaddr + 0x1000, vaddr + filesz);

            if (page_data_start < page_data_end) {
                uintptr_t copy_size = page_data_end - page_data_start;
                uintptr_t file_offset = offset + (page_data_start - vaddr);
                uintptr_t page_offset = page_data_start & 0xFFF;

                memcpy(file_data + file_offset, mapped + page_offset,
                       copy_size);
            }

            MAP(virtual, physical_memory_entry, PTR(current_page_vaddr));
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
    initialize_thread_files(thread);

    build_starting_stack(thread, PTR(header->entryPosition), phdr_virtual,
                         header->programHeaderEntryCount,
                         header->programHeaderEntrySize);

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
    thread->run = true;
    listAdd(&threads_to_process, thread);
    return thread;
}
