#include <process.h>
#include <stddef.h>

VirtualMemoryEntry *process_map_memory_simple(Process *process,
                                              PhysicalMemoryEntry *physical,
                                              void *address) {
    VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
    virtual->virtual = address;
    virtual->process = process;
    virtual->size = physical->page_count * 4096;
    virtual->mappings = NULL;

    MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
    mapping->virtual = address;
    mapping->physical = physical;
    physical->refcount++;
    mapping->copy_on_write = false;

    listAdd(&virtual->mappings, mapping);
    listAdd(&process->virtual_memory_entries, virtual);
    return virtual;
}

PhysicalMemoryEntry *get_single_page_physical_memory_entry() {
    void *physical = getPhysicalPage();
    PhysicalMemoryEntry *physical_memory_entry =
        malloc(sizeof(PhysicalMemoryEntry));
    physical_memory_entry->physical = physical;
    physical_memory_entry->refcount = 0;
    physical_memory_entry->page_count = 1;
    return physical_memory_entry;
}
