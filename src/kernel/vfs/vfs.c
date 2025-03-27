//
// Created by lukas on 3/27/25.
//

#include <process.h>
#include <file.h>

void handleMkFifoSyscall(ProcessThread *thread) {
    PipeFile *file = malloc(sizeof(PipeFile));
    char *mapped_name = mapTemporaryB(getPhysicalAddress(thread->process->memory_information.pageDirectory, PTR(thread->parameters[0])));
    uint32_t len = strlen(mapped_name);
    file->name = malloc(len + 1);
    uint32_t position = 0;
    mapped_name = mapTemporaryB(getPhysicalAddress(thread->process->memory_information.pageDirectory, PTR(thread->parameters[0])));
    while (mapped_name[position]) {
        file->name[position] = mapped_name[position];
        position++;
    }

    file->type = FILE_TYPE_PIPE;
    file->queue = NULL;
    file->blockedReadingThread = NULL;
    listAdd(&thread->process->container->vfs, file);
    thread->resume = true;
    thread->returnValue = 0;
}
