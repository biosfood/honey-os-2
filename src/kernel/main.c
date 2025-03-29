#include "memory/malloc.h"
#include <file.h>
#include <interrupts.h>
#include <memory.h>
#include <multiboot.h>
#include <process.h>
#include <service.h>
#include <stdint.h>
#include <syscall.h>
#include <util.h>

AllocationBlock *allocationData[12];

uint32_t id_counter = 1;

Process *newProcess(Container *container) {
    Process *process = malloc(sizeof(Process));
    process->id = id_counter++;
    process->container = container;
    listAdd(&container->processes, process);
    process->memory_information.pageDirectory = malloc(0x1000);
    process->cr3 =
        getPhysicalAddressKernel(process->memory_information.pageDirectory);
    process->threads = NULL;
    return process;
}


Container *newContainer(DirectoryFile *directory_file) {
    Container *container = malloc(sizeof(Container));
    container->id = id_counter++;
    container->processes = NULL;
    container->vfs = directory_file;

    Process *init_process = newProcess(container);
    File *init_file = findFile("/bin/init", container->vfs);
    if (!init_file || init_file->type != FILE_TYPE_INITRD) {
        // TODO: clean up
        free(container);
        return NULL;
    }
    processLoadELF(init_process, ((InitrdFile*)(void*)init_file)->kernelPosition);
    return container;
}

bool shutdown = false;

void loadAndScheduleSystemServices(void *multibootInfo) {
    // installKernelEvents();
    void *address = kernelMapPhysicalCount(multibootInfo, 4);
    uint32_t initrdSize;
    void *initrd = findInitrd(address, &initrdSize);
    DirectoryFile *initrdData = processInitrd(initrd, initrdSize);
    newContainer(initrdData);
}


void kernelMain(void *multibootInfo) {
    reservePagesUntilPhysical(0x900);
    loadAndScheduleSystemServices(multibootInfo);
    setupSyscalls();
    registerInterrupts();

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
