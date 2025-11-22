#include <process.h>

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
    listAdd(&threads_to_process, new_thread);
    thread->returnValue = new_thread->id;
    listAdd(&threads_to_process, thread);
}

void handleSetThreadPointerSyscall(ProcessThread *thread) {
    thread->thread_pointer_gs = thread->parameters[0];
    thread->returnValue = 0;
    listAdd(&threads_to_process, thread);
}
