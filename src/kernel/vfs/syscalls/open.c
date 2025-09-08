//
// Created by lukas on 9/5/25.
//
#include <process.h>
#include <stddef.h>
#include <vfs.h>

enum OpenStage {
    OPEN_PRE = 0,
    OPEN_MAIN,
    OPEN_POST,
};

struct OpenState {
    enum OpenStage stage;
    char *filename;
    void *scratchpad; // for FileSystem->getFile()
    File *file;
};

void handleOpenSyscall(ProcessThread *thread) {
    struct OpenState *state = (void*)thread->threadProcessingState;
    thread->run = false; // must explicitly be set.
    switch (state->stage) {
    case OPEN_PRE:
        state->filename = copy_string_from_process(thread->process,
                                                   PTR(thread->parameters[0]));
        state->scratchpad = NULL;
        state->file = NULL;
        state->stage = OPEN_MAIN;
        // fallthrough
    case OPEN_MAIN:
        FileOperationStatus status =
            thread->process->container->vfs->type->getFile(
                thread->process->container->vfs, state->filename, thread,
                &state->file, &state->scratchpad);
        if (status != FILE_OPERATION_DONE) {
            break;
        }
        state->stage = OPEN_POST;
        // fallthrough
    case OPEN_POST:
    post:
        if (state->file == NULL) {
            thread->returnValue = -1;
        } else {
            FileDescriptor *file_descriptor =
                allocateFileDescriptor(thread->process);
            file_descriptor->file = state->file;
            file_descriptor->process = thread->process;
            file_descriptor->offset = 0;
            thread->returnValue = file_descriptor->id;
            listAdd(&state->file->file_descriptors, file_descriptor);
            file_descriptor->path = state->filename; // taken from copy_string_from_process
        }
        thread->run = true;
        listAdd(&threads_to_process, thread);
    }
}
