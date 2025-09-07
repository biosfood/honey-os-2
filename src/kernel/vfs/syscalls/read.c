//
// Created by lukas on 8/20/25.
//
#include <process.h>
#include <stddef.h>
#include <vfs.h>

enum ReadStage {
    READ_SETUP = 0,
    READ_POST = 1,
};

struct ReadState {
    enum ReadStage stage;
    FileDescriptor *file_descriptor;
    void *data_buffer;
    uint32_t bytes_read;
};

void handleReadSyscall(ProcessThread *thread) {
    struct ReadState *state = (void*)thread->threadProcessingState;
    uint32_t bytes_to_transfer;
    switch (state->stage) {
    case READ_SETUP:
        state->file_descriptor = NULL;
        foreach (thread->process->openFileHandles, FileDescriptor *, descriptor,
                 {
                     if (thread->parameters[0] == descriptor->id) {
                         state->file_descriptor = descriptor;
                     }
                 })
            ;
        if (state->file_descriptor == NULL) {
            thread->returnValue = -1;
            listAdd(&threads_to_process, thread);
            return;
        }
        if (thread->parameters[2] == 0) {
            thread->returnValue = 0;
            listAdd(&threads_to_process, thread);
            return;
        }
        state->data_buffer = malloc(thread->parameters[2]);
        state->file_descriptor->file->file_system->type->read(
            state->file_descriptor->file, state->data_buffer,
            thread->parameters[2], thread->parameters[3], thread,
            state->file_descriptor, &state->bytes_read);
        thread->run = false;
        state->stage = READ_POST;
        break;
    case READ_POST:
        bytes_to_transfer = MIN(state->bytes_read, thread->parameters[2]);
        copy_from_kernel_to_process(state->data_buffer, thread->process,
                                    PTR(thread->parameters[1]),
                                    bytes_to_transfer);
        free(state->data_buffer);
        listAdd(&threads_to_process, thread);
        thread->run = true;
        thread->returnValue = bytes_to_transfer;
    }
}
