#include <process.h>

void handlePthreadCreateSyscall(ProcessThread *thread) {
    ProcessThread *new_thread = malloc(sizeof(ProcessThread));
    memset(new_thread, 0, sizeof(ProcessThread));
    new_thread->id = id_counter++;
    new_thread->process = thread->process;
    listAdd(&(thread->process->threads), new_thread);
    new_thread->function = 0;
    new_thread->esp = malloc(0x1000);
    sharePage(&new_thread->process->memory_information, new_thread->esp,
              new_thread->esp);
    new_thread->esp += 0x1000 - 0x10;
    *(void **)new_thread->esp = PTR(thread->parameters[2]);
    *(void **)(new_thread->esp + 0x8) = PTR(thread->parameters[3]);
    *(void **)(new_thread->esp + 0x4) = &runEnd;
    // TODO: here, this should behave as pthread_exit, the main() thread exit
    // must behave as exit()
    listAdd(&threads_to_process, new_thread);
    thread->returnValue = 0;
    thread->resume = true;
}
