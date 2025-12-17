//
// Created by lukas on 8/21/25.
//

#include <process.h>
#include <stddef.h>
#include <vfs.h>
#include <errno.h>

enum WriteStage {
    WRITE_SETUP = 0,
    WRITE_POST = 1,
};

struct WriteState {
    enum WriteStage stage;
    FileDescriptor *file_descriptor;
    void *data_buffer;
    uint32_t bytes_written;
};

void handleWriteSyscall(ProcessThread *thread) {
    struct WriteState *state = (void*)thread->threadProcessingState;
    switch (state->stage) {
    case WRITE_SETUP:
        state->file_descriptor = NULL;
        foreach (thread->process->openFileHandles, FileDescriptor *, descriptor,
                 {
                     if (thread->parameters[0] == descriptor->id) {
                         state->file_descriptor = descriptor;
                     }
                 })
            ;
        if (state->file_descriptor == NULL || !state->file_descriptor->write) {
            thread->returnValue = -EBADF;
            listAdd(&threads_to_process, thread);
            return;
        }
        if (thread->parameters[2] == 0) {
            thread->returnValue = 0;
            listAdd(&threads_to_process, thread);
            return;
        }
        state->data_buffer = copy_from_process_to_kernel(
            thread->process, PTR(thread->parameters[1]), thread->parameters[2]);
        state->file_descriptor->file->file_system->type->write(
            state->file_descriptor->file, state->data_buffer,
            thread->parameters[2], thread->parameters[3], thread,
            state->file_descriptor, &state->bytes_written);
        thread->run = false;
        state->stage = WRITE_POST;
        break;
    case WRITE_POST:
        free(state->data_buffer);
        thread->returnValue = state->bytes_written;
        thread->run = true;
        listAdd(&threads_to_process, thread);
    }
}
