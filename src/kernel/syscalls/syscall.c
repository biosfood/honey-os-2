#include <process.h>
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

void handleSyscall(void *esp, const uint32_t function, const uint32_t parameter0,
                   const uint32_t parameter1, const uint32_t parameter2,
                   const uint32_t parameter3) {
    if (!function) {
        // error condition, should not exist.
        current_thread->run = false;
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

extern uintptr_t handleForkSyscall;
extern uintptr_t handlePthreadCreateSyscall;
extern uintptr_t handleOpenSyscall, handleCloseSyscall;
extern uintptr_t handleReadSyscall;
extern uintptr_t handleWriteSyscall;
extern uintptr_t handleStatSyscall;
extern uintptr_t handleCreateFileSyscall;
extern uintptr_t handleMmapSyscall, handleMunmapSyscall;
extern uintptr_t handleExecSyscall;
extern uintptr_t handleSetThreadPointerSyscall;

void (*syscallHandlers[])(ProcessThread *) = {
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    (void *) &handleForkSyscall,
    NULL,
    (void *) &handlePthreadCreateSyscall,
    (void *) &handleOpenSyscall,
    (void *) &handleReadSyscall,
    (void *) &handleWriteSyscall,
    (void *) &handleCreateFileSyscall,
    (void *) &handleMmapSyscall,
    (void *) &handleMunmapSyscall,
    (void *) &handleCloseSyscall,
    (void *) &handleStatSyscall,
    (void *) &handleExecSyscall,
    (void *) &handleSetThreadPointerSyscall,
};

extern uint32_t thread_return_value, thread_cr3, thread_esp;

extern void (runFunction)();

// The GDT entry structure (packed is crucial)
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// defined in ASM
extern struct gdt_entry newGDT[];

// 0x30 is the offset of our TLS entry in the GDT (Index 6 * 8 bytes)
#define GDT_TLS_INDEX 6
#define TLS_SELECTOR  (0x30 | 3) // Index 6, RPL 3 (User)

void set_thread_area_32(uint32_t tp) {
    uint32_t base = tp;
    struct gdt_entry *tls_entry = &newGDT[GDT_TLS_INDEX];

    // Encode the pointer into the GDT entry split fields
    tls_entry->base_low    = base & 0xFFFF;
    tls_entry->base_middle = (base >> 16) & 0xFF;
    tls_entry->base_high   = (base >> 24) & 0xFF;

    // We must reload the segment register for the CPU to cache the new base.
    // If we are in kernel mode, we can load it now.
    // If we are returning to userspace, the `iret` or `sysexit` flow
    // must ensure gs is loaded with TLS_SELECTOR.

    // For kernel access to user TLS (optional, but good for debugging):
    asm volatile("mov %0, %%gs" :: "r"(TLS_SELECTOR));
}

void processThread(ProcessThread *thread) {
    if (thread->join_info.exited) {
        return;
    }
    if (thread->run) {
        set_thread_area_32(thread->thread_pointer_gs);
        thread_return_value = thread->returnValue;
        thread_esp = U32(thread->esp);
        thread_cr3 = U32(thread->process->cr3);
        current_thread = thread;
        runFunction();
        thread = current_thread;
        if (!thread || thread->process->reap_info.exited || thread->join_info.exited) {
            return;
        }
        memset(thread->threadProcessingState, 0, 32);
        thread->run = true;
    }

    if (thread->function && thread->function < sizeof(syscallHandlers) / sizeof(void *)) {
        void (*handler)(ProcessThread *) = syscallHandlers[thread->function];
        handler(thread);
    }
}
