//
// Created by lukas on 3/24/25.
//

#ifndef PROCESS_H
#define PROCESS_H

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

typedef struct Process {
    uint32_t id;
    Container *container;
    ListElement *threads;
    PagingInfo memory_information;
    void *cr3;
    ListElement *openFileHandles;
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

#endif // PROCESS_H
