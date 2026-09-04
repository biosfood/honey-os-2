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

typedef struct Container {
    uint32_t id;
    ListElement *processes;
    FileSystem *vfs;
    FileSystem *procfs;
} Container;

enum MemoryType {
    MEM_TYPE_PROGRAM_DATA,
    MEM_TYPE_STACK,
    MEM_TYPE_HEAP,
    MEM_TYPE_KERNEL,
    MEM_TYPE_PAGING,
    MEM_TYPE_MMAP_FILE,
    MEM_TYPE_MMIO,
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
    ProcessFile process_files[PROC_FILE_MAX];
    struct {
        bool exited, reaped;
        int32_t exit_code;
    } reap_info;
} Process;

typedef struct ProcessThread {
    uint32_t id;
    Process *process;
    // system call information
    // the gist is that in the kernel, a process is always in the 'paused' state
    // - because it did a system call.
    uint32_t function;
    uint32_t parameters[4];
    uint32_t returnValue;
    void *esp;
    bool run;
    // to be used as the thread pointer.
    // Since this register is not writable from ring 3, this needs to be set
    // from the kernel, so it is stored here. Normally, user programs should
    // assume all registers to be clobbered by a syscall but this one cannot be
    // restored otherwise.
    uint32_t thread_pointer_gs;
    uint32_t threadProcessingState[8];
    ThreadFile thread_files[THREAD_FILE_MAX];
    struct {
        bool exited, joined;
        void *result;
    } join_info;
} ProcessThread;

extern ListElement *threads_to_process;
extern uint32_t id_counter;
extern void *runEnd;

extern void processThread(ProcessThread *thread);
extern ProcessThread *processLoadELF(Process *process, void *file_data);
extern Process *newProcess(Container *container, char *exe);

extern char *copy_string_from_process(const Process *process,
                                      const void *const from);
extern void *copy_from_process_to_kernel(const Process *process,
                                         void *threadRead,
                                         const uint32_t bytes_to_transfer);
extern void copy_from_kernel_to_process(void *read, Process *process,
                                        void *threadWrite,
                                        uint32_t bytes_to_transfer);
extern void copy_between_processes(const Process *from_process, void *from,
                                   const Process *to_process, void *to,
                                   const uint32_t bytes_to_transfer);
extern void memcpy_proc_to_kernel(const Process *process, void *threadRead,
                                  uint8_t *write,
                                  const uint32_t bytes_to_transfer);
extern void process_exit(Process *process, int32_t return_code);
extern void thread_exit(ProcessThread *thread, void *result);
extern void terminate_thread(ProcessThread *thread);

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
