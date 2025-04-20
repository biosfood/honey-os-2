#include <hlib.h>

#include "include/syscalls.h"


uint32_t syscall(uint32_t function, uint32_t parameter0, uint32_t parameter1,
                 uint32_t parameter2, uint32_t parameter3) {
    uint32_t esp;
    asm("push %%eax" ::"a"(&&end)); // end: return address
    asm("mov %%esp, %%eax" : "=a"(esp));
    asm("sysenter\n"
        :
        : "a"(function), "b"(parameter0), "c"(parameter1), "d"(parameter2),
          "S"(parameter3), "D"(esp));
// eax is set by the kernel as the return value
end:
    // the 0x1C comes from the number of parameters / local variables do handle
    // this function with care or it will break everything
    asm("add $0x1C, %%esp\n"
        "pop %%ebp\n"
        "ret" ::);
    // don't go here! ret returns with the correct value
    return 0;
}

void *requestMemory(uint32_t pageCount, void *targetAddress,
                    void *physicalAddress) {
    return PTR(syscall(SYS_REQUEST_MEMORY, pageCount, U32(targetAddress),
                       U32(physicalAddress), 0));
}

void *getPage() { return requestMemory(1, NULL, NULL); }

void *getPagesCount(uint32_t count) { return requestMemory(count, NULL, NULL); }

void freePage(void *location) {
    syscall(SYS_FREE_PAGE, U32(location), 0, 0, 0);
}

#define REQUEST1(returnType, functionName, service, function)                  \
    returnType functionName(uint32_t data) {                                   \
        static uint32_t serviceId = 0;                                         \
        if (!serviceId) {                                                      \
            serviceId = getService(service);                                   \
            serviceId = getService(service);                                   \
        }                                                                      \
        static uint32_t functionId = 0;                                        \
        if (!functionId) {                                                     \
            functionId = getFunction(serviceId, function);                     \
        }                                                                      \
        return (returnType)request(serviceId, functionId, data, 0);            \
    }

#define REQUEST0(returnType, functionName, service, function)                  \
    returnType functionName() {                                                \
        static uint32_t serviceId = 0;                                         \
        if (!serviceId) {                                                      \
            serviceId = getService(service);                                   \
            serviceId = getService(service);                                   \
        }                                                                      \
        static uint32_t functionId = 0;                                        \
        if (!functionId) {                                                     \
            functionId = getFunction(serviceId, function);                     \
        }                                                                      \
        return (returnType)request(serviceId, functionId, 0, 0);               \
    }

REQUEST1(void, sleep, "pit", "sleep")
REQUEST0(bool, checkFocus, "ioManager", "checkFocus")
