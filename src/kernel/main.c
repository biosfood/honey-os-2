#include <util.h>
#include "memory/malloc.h"
#include <interrupts.h>
#include <memory.h>
#include <multiboot.h>
#include <service.h>
#include <stdint.h>
#include <syscall.h>
#include <util.h>
#include <process.h>

void *initrd;
uint32_t initrdSize;
Service *hlib;
AllocationBlock *allocationData[12];

Service *readInitrdProgram(char *name) {
    char *fileName = combineStrings("initrd/", name);
    void *elfData = findTarFile(initrd, initrdSize, fileName);
    free(fileName);
    if (elfData) {
        return loadElf(elfData, name);
    }
    return NULL;
}

Service *loadProgram(char *name, Thread *respondingTo, bool initialize) {
    Service *service = readInitrdProgram(name);
    if (initialize) {
        ServiceFunction *provider = findFunction(service, "main");
        scheduleFunction(provider, respondingTo);
    }
    return service;
}

void loadAndScheduleSystemServices(void *multibootInfo) {
    // installKernelEvents();
    void *address = kernelMapPhysicalCount(multibootInfo, 4);
    initrd = findInitrd(address, &initrdSize);
    // hlib = readInitrdProgram("hlib");
    // loadProgram("loader", NULL, true);
}

uint32_t id_counter = 1;

Container *newContainer() {
    Container *container = malloc(sizeof(Container));
    container->id = id_counter++;
    container->processes = NULL;
    container->vfs = NULL;
    return container;
}

Process *newProcess(Container *container) {
    Process *process = malloc(sizeof(Process));
    process->id = id_counter++;
    process->container = container;
    listAdd(&container->processes, process);
    process->memory_information.pageDirectory = malloc(0x1000);
    process->cr3 = getPhysicalAddressKernel(process->memory_information.pageDirectory);
    process->threads = NULL;
    return process;
}

extern void *functionsStart;
extern void *functionsEnd;

ProcessThread *processLoadELF(Process *process, void *elfStart) {
    // use this function ONLY to load the initrd/loader program(maybe also the
    // ELF loader service)!
    ElfHeader *header = elfStart;
    ProgramHeader *programHeader =
            elfStart + header->programHeaderTablePosition;
    // fire load event
    // fireEvent(loadInitrdEvent, service->nameHash, 0);
    void *current = &functionsStart;
    for (uint32_t i = 0; i < 3; i++) {
        // todo: make this unwritable!
        sharePage(&process->memory_information, current, current);
        current += 0x1000;
    }
    // reserve first few pages to hopefully catch NULL pointers correctly
    reservePagesCount(&(process->memory_information), 0, 0x10);
    for (uint32_t i = 0; i < header->programHeaderEntryCount; i++) {
        if (hlib && programHeader->virtualAddress >= 0xF0000000) {
            goto end;
        }
        for (uint32_t page = 0; page < programHeader->segmentMemorySize;
             page += 0x1000) {
            void *data = malloc(0x1000);
            if (programHeader->segmentFileSize > page) {
                memcpy(elfStart + programHeader->dataOffset + page, data,
                       MIN(0x1000, programHeader->segmentFileSize - page));
            }
            sharePage(&process->memory_information, data,
                      PTR(programHeader->virtualAddress + page));
        }
    end:
        programHeader = (void *) programHeader + header->programHeaderEntrySize;
    }
    ProcessThread *thread = malloc(sizeof(ProcessThread));
    memset(thread, 0, sizeof(ProcessThread));
    thread->id = id_counter++;
    thread->process = process;
    listAdd(&(process->threads), thread);
    thread->function = 0;
    thread->esp = malloc(0x1000);
    sharePage(&process->memory_information, thread->esp, thread->esp);
    thread->esp += 0x1000 - 0x10;
    *(void **) thread->esp = PTR(header->entryPosition);
    *(void **) (thread->esp + 0x4) = &runEnd;
    listAdd(&threads_to_process, thread);

    return thread;
}

bool shutdown = false;

void kernelMain(void *multibootInfo) {
    reservePagesUntilPhysical(0x900);
    loadAndScheduleSystemServices(multibootInfo);
    setupSyscalls();
    registerInterrupts();
    // TODO: read file to describe a container
    Container *container = newContainer();
    Process *init_process = newProcess(container);
    void *data = findTarFile(initrd, initrdSize, "initrd/init");
    processLoadELF(init_process, data);

    while (1) {
        ProcessThread *thread = listPopFirst(&threads_to_process);
        if (thread) {
            processThread(thread);
        } else {
            if (shutdown) {
                break;
            } else {
                asm("sti;hlt;cli");
            }
        }
    }
}
