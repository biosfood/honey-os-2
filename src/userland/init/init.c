#include <stdint.h>

#define PTR(x) ((void *)(uintptr_t)(x))
#define U32(x) ((uint32_t)(uintptr_t)(x))


typedef enum {
    SYS_RUN = 0,
    SYS_CREATE_FUNCTION = 1,
    SYS_REQUEST = 2,
    SYS_IO_IN = 3,
    SYS_IO_OUT = 4,
    SYS_LOAD_INITRD = 5,
    SYS_GET_SERVICE = 6,
    SYS_GET_FUNCTION = 7,
    SYS_SUBSCRIBE_INTERRUPT = 8,
    SYS_CREATE_EVENT = 9,
    SYS_GET_EVENT = 10,
    SYS_FIRE_EVENT = 11,
    SYS_SUBSCRIBE_EVENT = 12,
    SYS_GET_SERVICE_ID = 13,
    SYS_INSERT_STRING = 14,
    SYS_GET_STRING_LENGTH = 15,
    SYS_READ_STRING = 16,
    SYS_DISCARD_STRING = 17,
    SYS_REQUEST_MEMORY = 18,
    SYS_LOOKUP_SYMBOL = 19,
    SYS_STACK_CONTAINS = 20,
    SYS_AWAIT = 21,
    SYS_GET_PHYSICAL = 22,
    SYS_FORK = 23,
    SYS_FREE_PAGE = 24,
    SYS_PTHREAD_CREATE = 25,
} SyscallIds;

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

uint32_t ioIn(uint16_t port, uint8_t size) {
    return syscall(SYS_IO_IN, size, port, 0, 0);
}



void ioOut(uint16_t port, uint32_t value, uint8_t size) {
    syscall(SYS_IO_OUT, size, port, value, 0);
}

void parallelOut(uint32_t data, uint32_t dataLength) {
    if (data == '\n') {
        parallelOut('\r', 0);
    }
    uint8_t control;
    while (!(ioIn(0x379, sizeof(uint8_t)) & 0x80)) {
    }
    ioOut(0x378, U32(data), sizeof(uint8_t));

    control = ioIn(0x37A, sizeof(uint8_t));
    ioOut(0x37A, control | 1, sizeof(uint8_t));
    ioOut(0x37A, control, sizeof(uint8_t));
    while (!(ioIn(0x379, sizeof(uint8_t)) & 0x80)) {
    }
}

void writeBulk(char *buffer) {
    while (*buffer) {
        parallelOut(*buffer, 0);
        buffer++;
    }
}

int pthread_create(void *thread, void *attr, void*(*start_routine)(void*), void *restrict arg) {
    return syscall(SYS_PTHREAD_CREATE, U32(thread), U32(attr), U32(start_routine), U32(arg));
}

int test() {
    writeBulk("From the test function\n");
    return 0;
}

int main() {
    writeBulk("Hello World!\n");
    pthread_create(0, 0, (void*) test, 0);
}