#include <process.h>
#include <stddef.h>

void handlePthreadCreateSyscall(ProcessThread *thread) {
    ProcessThread *new_thread = malloc(sizeof(ProcessThread));
    memset(new_thread, 0, sizeof(ProcessThread));
    new_thread->id = id_counter++;
    new_thread->process = thread->process;
    listAdd(&(thread->process->threads), new_thread);
    new_thread->function = 0;
    new_thread->esp = PTR(thread->parameters[0]);
    new_thread->thread_pointer_gs = thread->parameters[1];
    new_thread->run = true;
    initialize_thread_files(new_thread);

    listAdd(&threads_to_process, new_thread);
    thread->returnValue = new_thread->id;
    listAdd(&threads_to_process, thread);
}

void handleSetThreadPointerSyscall(ProcessThread *thread) {
    thread->thread_pointer_gs = thread->parameters[0];
    thread->returnValue = 0;
    listAdd(&threads_to_process, thread);
}

void terminate_thread(ProcessThread *thread) {
    if (thread->join_info.exited) {
        return;
    }
    thread->join_info.exited = true;
    thread->join_info.joined = false;
    thread->run = false;
}

void thread_exit(ProcessThread *thread, void *result) {
    terminate_thread(thread);

    thread->join_info.result = result;
    thread->join_info.exited = true;
    uint32_t bytes_written = 0;
    fifo_write((File *)&thread->thread_files[THREAD_FILE_STATUS],
               &thread->join_info.result, 4, &bytes_written, NULL);
    if (bytes_written) {
        thread->join_info.joined = true;
    }
}