#include "service.h"
#include "elf.h"
#include <memory.h>
#include <process.h>
#include <service.h>
#include <stdarg.h>
#include <stddef.h>
#include <stringmap.h>
#include <util.h>

extern void *functionsStart;
extern void *functionsEnd;
extern void(runFunction)();

ListElement *services, *threads_to_process;
// Thread *current_thread;
extern Event *loadInitrdEvent;

void resume(Thread *thread) {
    if (U32(thread) < 0x1000) {
        asm("hlt" ::"a"(thread));
    }
    // current_thread = thread;
    runFunction();
}

Service *findService(char *name) {
    foreach (services, Service *, service, {
        if (stringEquals(service->name, name)) {
            return service;
        }
    })
    return NULL;
}

ServiceFunction *findFunction(Service *service, char *name) {
    foreach (service->functions, ServiceFunction *, provider, {
        if (stringEquals(provider->name, name)) {
            return provider;
        }
    })
    return NULL;
}

void scheduleFunction(ServiceFunction *provider, Thread *respondingTo, ...) {
    va_list valist;
    va_start(valist, respondingTo);
    uint32_t parameterCount = 0;
    while (va_arg(valist, uint32_t) || parameterCount <= 3) {
        parameterCount++;
    }
    va_start(valist, respondingTo);

    Thread *runCall = malloc(sizeof(Thread));
    runCall->function = 0;
    runCall->esp = malloc(0x1000); // todo: free this
    runCall->respondingTo = respondingTo;
    runCall->cr3 =
        getPhysicalAddressKernel(provider->service->pagingInfo.pageDirectory);
    runCall->service = provider->service;
    runCall->resume = true;
    sharePage(&provider->service->pagingInfo, runCall->esp, runCall->esp);
    runCall->esp += 0x1000 - 0x10 - (parameterCount * 0x4);
    *(void **)runCall->esp = provider->address;
    *(void **)(runCall->esp + 0x4) = &runEnd;
    for (uint32_t i = 0; i < parameterCount; i++) {
        *(uint32_t *)(runCall->esp + 0x8 + 4 * i) = va_arg(valist, uint32_t);
    }
    // listAdd(&threads_to_process, runCall);
}
