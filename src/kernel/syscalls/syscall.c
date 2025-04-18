#include <memory.h>
#include <process.h>
#include <service.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <util.h>

extern void (syscallStub)();

void writeMsrRegister(uint32_t reg, void *value) {
    // when transitioning to 64 bit: U64(value) >> 32);
    asm("wrmsr" ::"a"(U32(value)), "d"(0), "c"(reg));
}

void setupSyscalls() {
    writeMsrRegister(0x174, PTR(0x08)); // code segment register
    writeMsrRegister(0x175, malloc(0x1000) + 0x1000); // handler stack
    writeMsrRegister(0x176, syscallStub); // the handler
}

ProcessThread *current_thread;

void finalizeThread(ProcessThread *thread, uint32_t returnCode) {
    // if a function has finished all of its work, free the stack and resume the
    // super call (if it exists)
    freePageFrom(&thread->process->memory_information, thread->esp);
    freePage(thread->esp);
    listRemoveValue(&thread->process->threads, thread);
    if (listCount(thread->process->threads) == 0) {
        // TODO: give up everything for the process
        // free(thread->process);
    }
    free(thread);
    // todo: handle join()
}

void handleSyscall(void *esp, const uint32_t function, const uint32_t parameter0,
                   const uint32_t parameter1, const uint32_t parameter2,
                   const uint32_t parameter3) {
    if (!function) {
        finalizeThread(current_thread, parameter0);
        current_thread = NULL;
        return;
    }
    current_thread->function = function;
    current_thread->esp = esp;
    // todo: remove the cr3 parameter from the syscall struct
    current_thread->parameters[0] = parameter0;
    current_thread->parameters[1] = parameter1;
    current_thread->parameters[2] = parameter2;
    current_thread->parameters[3] = parameter3;
}

extern uintptr_t handleCreateFunctionSyscall;
extern uintptr_t handleRequestSyscall;
extern uintptr_t handleGetServiceSyscall;
extern uintptr_t handleGetFunctionSyscall;
extern uintptr_t handleSubscribeInterruptSyscall;
extern uintptr_t handleCreateEventSyscall;
extern uintptr_t handleGetEventSyscall;
extern uintptr_t handleFireEventSyscall;
extern uintptr_t handleSubscribeEventSyscall;
extern uintptr_t handleGetServiceIdSyscall;
extern uintptr_t handleInsertStringSyscall;
extern uintptr_t handleReadStringLengthSyscall;
extern uintptr_t handleReadStringSyscall;
extern uintptr_t handleDiscardStringSyscall;
extern uintptr_t handleRequestMemorySyscall;
extern uintptr_t handleLookupSymbolSyscall;
extern uintptr_t handleStackContainsSyscall;
extern uintptr_t handleAwaitSyscall;
extern uintptr_t handleGetPhysicalSyscall;
extern uintptr_t handleForkSyscall;
extern uintptr_t handleFreeSyscall;
extern uintptr_t handlePthreadCreateSyscall;
extern uintptr_t handleOpenSyscall;
extern uintptr_t handleReadSyscall;
extern uintptr_t handleWriteSyscall;
extern uintptr_t handleCreateFileSyscall;

void (*syscallHandlers[])(ProcessThread *) = {
    0,
    (void *) &handleCreateFunctionSyscall,
    (void *) &handleRequestSyscall,
    NULL,
    NULL,
    NULL,
    (void *) &handleGetServiceSyscall,
    (void *) &handleGetFunctionSyscall,
    (void *) &handleSubscribeInterruptSyscall,
    (void *) &handleCreateEventSyscall,
    (void *) &handleGetEventSyscall,
    (void *) &handleFireEventSyscall,
    (void *) &handleSubscribeEventSyscall,
    (void *) &handleGetServiceIdSyscall,
    (void *) &handleInsertStringSyscall,
    (void *) &handleReadStringLengthSyscall,
    (void *) &handleReadStringSyscall,
    (void *) &handleDiscardStringSyscall,
    (void *) &handleRequestMemorySyscall,
    (void *) &handleLookupSymbolSyscall,
    (void *) &handleStackContainsSyscall,
    (void *) &handleAwaitSyscall,
    (void *) &handleGetPhysicalSyscall,
    (void *) &handleForkSyscall,
    (void *) &handleFreeSyscall,
    (void *) &handlePthreadCreateSyscall,
    (void *) &handleOpenSyscall,
    (void *) &handleReadSyscall,
    (void *) &handleWriteSyscall,
    (void *) &handleCreateFileSyscall,
};

extern uint32_t thread_return_value, thread_cr3, thread_esp;

extern void (runFunction)();

void processThread(ProcessThread *thread) {
    thread_return_value = thread->returnValue;
    thread_esp = U32(thread->esp);
    thread_cr3 = U32(thread->process->cr3);
    current_thread = thread;
    runFunction();
    thread = current_thread;

    if (thread && thread->function && thread->function < sizeof(syscallHandlers) / sizeof(void *)) {
        thread->resume = false;
        void (*handler)(ProcessThread *) = syscallHandlers[thread->function];
        handler(thread);
        if (thread->resume) {
            listAdd(&threads_to_process, thread);
        }
    }
}
