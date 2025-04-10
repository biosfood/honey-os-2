#include "memory/malloc.h"
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

File *findFile(char *path, ListElement *mountlist) {
    Mount *mount = NULL;
    uint32_t current_match_length = 0;
    foreach (mountlist, Mount *, current_mount, {
        uint32_t match_length = 0;
        bool matches = true;
        while (current_mount->mountPoint[match_length]) {
            if (current_mount->mountPoint[match_length] != path[match_length]) {
                matches = false;
                break;
            }
            match_length++;
        }
        if (!matches) {
            continue;
        }
        if (match_length > current_match_length) {
            mount = current_mount;
            current_match_length = match_length;
        }
    })
        ;
    if (!mount) {
        return NULL;
    }
    char *realpath =
        combineStrings(mount->pathOffset, path + strlen(mount->mountPoint));
    char *nextStep = malloc(strlen(realpath) + 1);
    memset(nextStep, 0, strlen(realpath) + 1);
    uint32_t position = 0;
    uint32_t pathPosition = strlen(mount->mountPoint);
    for (int i = 0; i < strlen(mount->pathOffset); i++) {
        nextStep[position] = realpath[position];
        position++;
    }
    if (realpath[0] == '/' && !realpath[1]) {
        free(realpath);
        free(nextStep);
        return mount->file_system->type->getFile(mount->file_system, "/");
    }
    File *file = NULL;
    while (position != strlen(realpath)) {
        while (path[pathPosition] && path[pathPosition] != '/') {
            nextStep[position] = path[pathPosition];
            position++;
            pathPosition++;
        }
        file = mount->file_system->type->getFile(mount->file_system, nextStep);
        if (file->type == FILE_TYPE_LINK) {
            // TODO
            // if symbolic link, findFile(newPath, mountlist)
            while (1)
                ;
        }
        if (file->type == FILE_TYPE_DIRECTORY && position != strlen(realpath)) {
            nextStep[position] = path[pathPosition];
            position++;
            pathPosition++;
            continue;
        }
        break;
    }

    free(realpath);
    free(nextStep);
    return file;
}

Container *newContainer(ListElement *mountlist) {
    Container *container = malloc(sizeof(Container));
    container->id = id_counter++;
    container->processes = NULL;
    container->vfs = mountlist;
    // TODO: container-owned mounts?
    // I may want a special kernel file system for showing process information,
    // etc. at /proc this will provide a simple way for the kernel to exchange
    // information with user processes

    Process *init_process = newProcess(container);

    File *init_file = findFile("/bin/init", container->vfs);
    processLoadELF(init_process, init_file);
    return container;
}

bool shutdown = false;

void loadAndScheduleSystemServices(void *multibootInfo) {
    FileSystem *ramfs = createRamfs();
    Mount *mount = malloc(sizeof(Mount));
    mount->file_system = ramfs;
    mount->mountPoint = "/";
    mount->pathOffset = "/";
    ListElement *mounts = NULL;
    listAdd(&mounts, mount);
    listAdd(&ramfs->mountedInstances, mount);

    void *address = kernelMapPhysicalCount(multibootInfo, 4);
    uint32_t initrdSize;
    void *initrd = findInitrd(address, &initrdSize);
    processInitrd(initrd, initrdSize, ramfs);
    freePage(address);
    freePage(initrd);

    newContainer(mounts);
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
