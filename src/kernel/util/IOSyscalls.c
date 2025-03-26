#include <process.h>
#include <service.h>

void handleIOInSyscall(ProcessThread *thread) {
    switch (thread->parameters[0]) {
    case 1:
        asm("in %%dx, %%al"
            : "=a"(thread->returnValue)
            : "d"(thread->parameters[1]));
        break;
    case 2:
        asm("in %%dx, %%ax"
            : "=a"(thread->returnValue)
            : "d"(thread->parameters[1]));
        break;
    case 4:
        asm("in %%dx, %%eax"
            : "=a"(thread->returnValue)
            : "d"(thread->parameters[1]));
        break;
    }
    thread->resume = true;
}

void handleIOOutSyscall(ProcessThread *thread) {
    switch (thread->parameters[0]) {
    case 1:
        asm("out %0, %1"
            :
            : "a"((uint8_t)thread->parameters[2]), "Nd"(thread->parameters[1]));
        break;
    case 2:
        asm("out %0, %1"
            :
            : "a"((uint16_t)thread->parameters[2]), "Nd"(thread->parameters[1]));
        break;
    case 4:
        asm("out %0, %1"
            :
            : "a"((uint32_t)thread->parameters[2]), "Nd"(thread->parameters[1]));
        break;
    }
    thread->resume = true;
}
