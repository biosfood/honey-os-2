//
// Created by lukas on 9/5/25.
//
#include <process.h>
#include <stddef.h>
#include <vfs.h>
#include "fnctl.h"

void handleOpenSyscall(ProcessThread *thread) {
    char *filename =
        copy_string_from_process(thread->process, PTR(thread->parameters[0]));
    File *file;
    void *scratchpad = NULL;
    thread->process->container->vfs->type->getFile(
        thread->process->container->vfs, filename, thread, &file, &scratchpad);
    listAdd(&threads_to_process, thread);
    if (file == NULL) {
        if (thread->parameters[1] & O_CREAT) {
            char *directoryFileName = malloc(strlen(filename));
            memcpy(filename, directoryFileName, strlen(filename) + 1);
            int lastSlashPosition = strlen(filename) - 1;
            while (lastSlashPosition &&
                   directoryFileName[lastSlashPosition] != '/') {
                lastSlashPosition--;
                   }
            directoryFileName[lastSlashPosition] = 0;
            File *dir;
            scratchpad = NULL;
            thread->process->container->vfs->type->getFile(
                thread->process->container->vfs, directoryFileName, thread, &dir, &scratchpad);
            if (dir->type != FILE_TYPE_DIRECTORY) {
                thread->returnValue = -1;
                return;
            }
            file = dir->file_system->type->create(
                dir, filename + lastSlashPosition + 1, FILE_TYPE_FILE);
        } else {
            thread->returnValue = -1;
            return;
        }
    }
    free(filename);
    FileDescriptor *file_descriptor = allocateFileDescriptor(thread->process);
    file_descriptor->file = file;
    file_descriptor->process = thread->process;
    file_descriptor->offset = 0;
    thread->returnValue = file_descriptor->id;
    listAdd(&file->file_descriptors, file_descriptor);
}

