#include "memory/malloc.h"
#include "mountlistfs.h"
#include <interrupts.h>
#include <memory.h>
#include <multiboot.h>
#include <process.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <util.h>
#include <vfs.h>

AllocationBlock *allocationData[12];

uint32_t id_counter = 1;

Container *newContainer(FileSystem *fs) {
    Container *container = malloc(sizeof(Container));
    container->id = id_counter++;
    container->processes = NULL;
    container->vfs = fs;
    // TODO: container-owned mounts?
    // I may want a special kernel file system for showing process information,
    // etc. at /proc this will provide a simple way for the kernel to exchange
    // information with user processes

    Process *init_process = newProcess(container);

    File *init_file = fs->type->getFile((void *)fs, "/bin/init");
    processLoadELF(init_process, init_file);

    File *nulldev = fs->type->getFile((void *)fs, "/kernel/null");

    // standard IO streams opened before program starts
    FileDescriptor *stdin = allocateFileDescriptor(init_process);
    stdin->file = nulldev;
    stdin->process = (void *)init_process;
    listAdd(&init_process->openFileHandles, stdin);

    FileDescriptor *stdout = allocateFileDescriptor(init_process);
    stdout->file = nulldev;
    stdout->process = (void *)init_process;
    listAdd(&init_process->openFileHandles, stdout);

    FileDescriptor *stderr = allocateFileDescriptor(init_process);
    stderr->file = nulldev;
    stderr->process = (void *)init_process;
    listAdd(&init_process->openFileHandles, stderr);

    return container;
}

bool shutdown = false;
extern FileSystem cpuid_file_system, port_file_system, interrupt_file_system;

void loadAndScheduleSystemServices(void *multibootInfo) {
    FileSystem *fs = create_mount_list_file_system();

    FileSystem *ramfs = createRamfs();
    mount(fs, ramfs, "/", "/");
    FileSystem *kernelFs = createKernelFs();
    mount(fs, kernelFs, "/kernel/", "/");
    mount(fs, &cpuid_file_system, "/dev/cpuid/", "/");
    mount(fs, &port_file_system, "/dev/port/", "/");
    mount(fs, &interrupt_file_system, "/dev/interrupt/", "/");

    void *address = kernelMapPhysicalCount(multibootInfo, 4);
    uint32_t initrdSize;
    void *initrd = findInitrd(address, &initrdSize);
    processInitrd(initrd, initrdSize, ramfs);
    freePage(address);
    freePage(initrd);

    newContainer(fs);
}

void kernelMain(void *multibootInfo) {
    asm("cli");
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
