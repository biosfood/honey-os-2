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
#include <file.h>

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


bool shutdown = false;

void kernelMain(void *multibootInfo) {
    reservePagesUntilPhysical(0x900);
    loadAndScheduleSystemServices(multibootInfo);
    setupSyscalls();
    registerInterrupts();
    // TODO: read file to describe a container
    Container *container = newContainer();
    Process *init_process = newProcess(container);

    PipeFile *file = malloc(sizeof(PipeFile));
    file->name = "/dev/1";
    file->type = FILE_TYPE_PIPE;
    file->queue = NULL;
    file->blockedReadingThread = NULL;
    listAdd(&container->vfs, file);

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
