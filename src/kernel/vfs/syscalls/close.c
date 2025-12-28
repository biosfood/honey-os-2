//
// Created by lukas on 12/24/25.
//
#include <process.h>
#include <vfs.h>
#include <sys/stat.h>
#include <stddef.h>

void handleCloseSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
        if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
        }
    })
        ;
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        listAdd(&threads_to_process, thread);
        return;
    }
    listRemoveValue(&thread->process->openFileHandles, file_descriptor);
    listRemoveValue(&file_descriptor->file->file_descriptors, file_descriptor);
    free(file_descriptor->path);
    free(file_descriptor);
    thread->returnValue = 0;
    listAdd(&threads_to_process, thread);
}

