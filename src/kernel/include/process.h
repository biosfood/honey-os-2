//
// Created by lukas on 3/24/25.
//

#ifndef PROCESS_H
#define PROCESS_H

struct VirtualMemoryEntry;
struct PhysicalMemoryEntry;
struct MemoryMapping;

#include <memory.h>
#include <stdint.h>
#include <util.h>
#include <vfs.h>

typedef struct {
    char *name;
    void *data;
    uint32_t size;
} VFSFile;
struct DirectoryFile;
typedef struct {
    uint32_t id;
    ListElement *processes;
    FileSystem *vfs;
} Container;

enum MemoryType {
    MEM_TYPE_PROGRAM_DATA,
    MEM_TYPE_STACK,
    MEM_TYPE_HEAP,
    MEM_TYPE_KERNEL,
    MEM_TYPE_PAGING,
    MEM_TYPE_MMAP_FILE,
};

typedef struct MemoryMapping {
    struct PhysicalMemoryEntry *physical;
    void *virtual;
    bool copy_on_write;
} MemoryMapping;

// this is basically an entry in the page translation table.
typedef struct VirtualMemoryEntry {
    ListElement *mappings;
    // if virtual is null, the page won't be mapped to userspace.
    // used for example for the page table.
    void *virtual;
    uint32_t size;
    enum MemoryType type;
    struct Process *process;
} VirtualMemoryEntry;

typedef struct PhysicalMemoryEntry {
    void *physical;
    uint32_t page_count;
    uint32_t refcount;
} PhysicalMemoryEntry;

typedef struct Process {
    uint32_t id;
    Container *container;
    ListElement *threads;
    PagingInfo memory_information;
    void *cr3;
    ListElement *openFileHandles;
    ListElement *virtual_memory_entries;
} Process;

typedef struct ProcessThread {
    uint32_t id;
    Process *process;
    bool hasBeenJoined;
    bool readyToBeJoined;
    // system call information
    // the gist is that in the kernel, a process is always in the 'paused' state
    // - because it did a system call.
    uint32_t function;
    uint32_t parameters[4];
    uint32_t returnValue;
    void *esp;
    bool resume;
    bool avoidReschedule;
} ProcessThread;

extern ListElement *threads_to_process;
extern uint32_t id_counter;
extern void *runEnd;

extern void processThread(ProcessThread *thread);
extern ProcessThread *processLoadELF(Process *process, File *elfStart);
extern Process *newProcess(Container *container);

extern char *copy_string_from_process(const Process *process,
                                      const void *const from);
extern void *copy_from_process_to_kernel(const Process *process,
                                         void *threadRead,
                                         const uint32_t bytes_to_transfer);
extern void copy_from_kernel_to_process(uint8_t *read, Process *process,
                                        uint8_t *threadWrite,
                                        uint32_t bytes_to_transfer);
extern void copy_between_processes(const ProcessThread *readThread, void *from,
                                   const ProcessThread *writeThread, void *to,
                                   const uint32_t bytes_to_transfer);
// memory operations
extern VirtualMemoryEntry *
process_map_memory_simple(Process *process, PhysicalMemoryEntry *physical,
                          void *address);
extern PhysicalMemoryEntry *get_single_page_physical_memory_entry();
#define MAP(v, physical_mapping, address)                                      \
    {                                                                          \
        MemoryMapping *mapping = malloc(sizeof(MemoryMapping));                \
        mapping->virtual = address;                                            \
        mapping->physical = physical_mapping;                                  \
        physical_mapping->refcount++;                                          \
        mapping->copy_on_write = false;                                        \
        listAdd(&v->mappings, mapping);                                        \
    }

#endif // PROCESS_H
