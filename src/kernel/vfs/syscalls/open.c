//
// Created by lukas on 9/5/25.
//
#include "fnctl.h"
#include <process.h>
#include <stddef.h>
#include <vfs.h>

enum OpenStage {
    OPEN_PRE = 0,
    OPEN_MAIN,
    OPEN_CREATE_FIND,
    OPEN_CREATE_POST,
    OPEN_POST,
};

struct OpenState {
    enum OpenStage stage;
    char *filename;
    char *directory_file_name;
    void *scratchpad; // for FileSystem->getFile()
    File *file;
    File *directory;
    uint32_t lastSlashPosition;
};

void handleOpenSyscall(ProcessThread *thread) {
    struct OpenState *state = thread->threadProcessingState;
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
        if (state->file || !(thread->parameters[1] & O_CREAT)) {
            // found it
            state->stage = OPEN_POST;
            goto post;
        }
        // file not found and O_CREAT is set, need to create it now...
        state->stage = OPEN_CREATE_FIND;
        state->directory_file_name = malloc(strlen(state->filename));
        memcpy(state->filename, state->directory_file_name,
               strlen(state->filename) + 1);
        state->lastSlashPosition = strlen(state->filename) - 1;
        while (state->lastSlashPosition &&
               state->directory_file_name[state->lastSlashPosition] != '/') {
            state->lastSlashPosition--;
        }
        state->directory_file_name[state->lastSlashPosition] = 0;
        // fallthrough
    case OPEN_CREATE_FIND:
        status = thread->process->container->vfs->type->getFile(
            thread->process->container->vfs, state->directory_file_name, thread,
            &state->directory, &state->scratchpad);
        if (status != FILE_OPERATION_DONE) {
            break;
        }
        if (state->directory == NULL ||
            state->directory->type != FILE_TYPE_DIRECTORY) {
            state->stage = OPEN_POST;
            goto post;
        }
        state->stage = OPEN_CREATE_POST;
        // fallthrough
    case OPEN_CREATE_POST:
        state->file = state->directory->file_system->type->create(
            state->directory, state->filename + state->lastSlashPosition + 1,
            FILE_TYPE_FILE);
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
        }
        free(state->filename);
        if (state->directory_file_name) {
            free(state->directory_file_name);
        }
        thread->run = true;
        listAdd(&threads_to_process, thread);
    }
}
