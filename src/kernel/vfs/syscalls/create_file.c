//
// Created by lukas on 9/7/25.
//
#include <process.h>
#include <stddef.h>
#include <vfs.h>

void handleCreateFileSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
        if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
        }
    })
        ;
    if (!file_descriptor || file_descriptor->file->type != FILE_TYPE_DIRECTORY) {
        thread->returnValue = -1;
        goto end;
    }

    char *filename =
        copy_string_from_process(thread->process, PTR(thread->parameters[1]));
    uint8_t length = strlen(filename);
    if (!length) {
        thread->returnValue = -1;
        goto end;
    }
    for (uint8_t i = 0; filename[i]; i++) {
        if (filename[i] == '/') {
            thread->returnValue = -1;
            goto end;
        }
    }

    int status = file_descriptor->file->file_system->type->create(
        file_descriptor->file, filename, thread->parameters[2]);
    thread->returnValue = status ? -1 : 0;
end:
    listAdd(&threads_to_process, thread);
}
