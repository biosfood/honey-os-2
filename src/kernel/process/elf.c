#include "elf.h"
#include <process.h>
#include <stddef.h>

// You'll need to define the AT_* values. They are standard in <elf.h>.
#define AT_NULL 0  /* End of vector */
#define AT_PHDR 3  /* Phdr base address */
#define AT_PHNUM 5 /* Number of phdrs */
#define AT_PHENT 4 /* Size of one phdr */

void build_starting_stack(ProcessThread *thread, void *start_position,
                          uintptr_t phdr_vaddr, int phnum, int phentsize,
                          int argc, char **argv, int envc, char **envp) {
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

    // Pre-allocate additional 15 pages (64KB total) of stack space
    for (int i = 0; i < 15; i++) {
        PhysicalMemoryEntry *extra_phys = get_single_page_physical_memory_entry();
        extra_phys->refcount = 1;
        void *zeroed = mapTemporaryA(extra_phys->physical);
        memset(zeroed, 0, 4096);
        MemoryMapping *extra_mapping = malloc(sizeof(MemoryMapping));
        extra_mapping->physical = extra_phys;
        extra_mapping->virtual = ADDRESS(PAGE_ID(virtual->virtual) + 2047 - 15 + i);
        extra_mapping->copy_on_write = false;
        listAdd(&virtual->mappings, extra_mapping);
    }

    listAdd(&process->virtual_memory_entries, virtual);

    char **user_argv_ptrs = argc > 0 ? malloc(sizeof(char *) * argc) : NULL;
    char **user_envp_ptrs = envc > 0 ? malloc(sizeof(char *) * envc) : NULL;

    void *stack_page_base = mapTemporaryA(physical->physical);
    char *stack_ptr = (char *)stack_page_base + 4096;
    char *stack_top_kernel = stack_ptr;
    char *stack_top_virtual = (char *)(virtual->virtual + virtual->size);

    // 1. Push environment variable strings (if any)
    for (int i = envc - 1; i >= 0; i--) {
        uint32_t len = strlen(envp[i]) + 1;
        stack_ptr -= len;
        memcpy(envp[i], stack_ptr, len);
        uintptr_t offset = stack_top_kernel - stack_ptr;
        user_envp_ptrs[i] = stack_top_virtual - offset;
    }

    // 2. Push argument strings
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t len = strlen(argv[i]) + 1;
        stack_ptr -= len;
        memcpy(argv[i], stack_ptr, len);
        uintptr_t offset = stack_top_kernel - stack_ptr;
        user_argv_ptrs[i] = stack_top_virtual - offset;
    }

    // Table elements to push:
    // - Auxv: 4 pairs (AT_PHDR, phdr_vaddr, AT_PHNUM, phnum, AT_PHENT, phentsize, AT_NULL, 0) = 8 words
    // - envp NULL sentinel = 1 word
    // - envp pointers = envc words
    // - argv NULL sentinel = 1 word
    // - argv pointers = argc words
    // - argc = 1 word
    uint32_t total_table_words = argc + envc + 11;
    uint32_t total_table_bytes = total_table_words * sizeof(uint32_t);

    // Align stack pointer so user %esp at _start (pointing to argc) is 16-byte aligned.
    uintptr_t cur_esp = (uintptr_t)(stack_ptr - total_table_bytes);
    uintptr_t aligned_esp = cur_esp & ~15L;
    uintptr_t padding = cur_esp - aligned_esp;
    stack_ptr -= padding;
    memset(stack_ptr, 0, padding);

    // 3. Push Auxiliary Vector (auxv) in reverse order
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

    // 4. Push envp NULL sentinel
    stack_ptr -= sizeof(char *);
    *(char **)stack_ptr = NULL;

    // 5. Push envp pointers in reverse order (envc - 1 down to 0)
    for (int i = envc - 1; i >= 0; i--) {
        stack_ptr -= sizeof(char *);
        *(char **)stack_ptr = user_envp_ptrs[i];
    }

    // 6. Push argv NULL sentinel
    stack_ptr -= sizeof(char *);
    *(char **)stack_ptr = NULL;

    // 7. Push argv pointers in reverse order (argc - 1 down to 0)
    for (int i = argc - 1; i >= 0; i--) {
        stack_ptr -= sizeof(char *);
        *(char **)stack_ptr = user_argv_ptrs[i];
    }

    // 8. Push argc
    stack_ptr -= sizeof(int);
    *(int *)stack_ptr = argc;

    // 9. Push start position for kernel sysexit return
    stack_ptr -= sizeof(void *);
    *(void **)stack_ptr = start_position;

    if (user_argv_ptrs) {
        free(user_argv_ptrs);
    }
    if (user_envp_ptrs) {
        free(user_envp_ptrs);
    }

    uintptr_t final_offset = stack_top_kernel - stack_ptr;
    thread->esp = stack_top_virtual - final_offset;
}
extern void *functionsStart;
extern void *functionsEnd;

PhysicalMemoryEntry kernel_functions_physical = {.physical = 0, 4, 0};

ProcessThread *processLoadELF(Process *process, void *file_data, int argc,
                             char **argv, int envc, char **envp) {
    char *default_argv[] = {"/init"};
    if (argc <= 0 || argv == NULL) {
        argc = 1;
        argv = default_argv;
    }
    if (envc < 0 || envp == NULL) {
        envc = 0;
        envp = NULL;
    }
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
                         header->programHeaderEntrySize,
                         argc, argv, envc, envp);

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
