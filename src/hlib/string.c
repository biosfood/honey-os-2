#include <hlib.h>
#include <stdint.h>
#include <syscalls.h>

size_t strlen(const char *string) {
    if (!string) {
        return 0;
    }
    uint32_t size = 0;
    while (*string) {
        string++;
        size++;
    }
    return size;
}

void memcpy(void *from, void *to, uint32_t size) {
    uint8_t *a = from, *b = to;
    for (uint32_t i = 0; i < size; i++) {
        b[i] = a[i];
    }
}

uintptr_t insertString(char *string) {
    return syscall(SYS_INSERT_STRING, U32(string), 0, 0, 0);
}

uintptr_t getStringLength(uintptr_t stringId) {
    return syscall(SYS_GET_STRING_LENGTH, stringId, 0, 0, 0);
}

void readString(uintptr_t stringId, void *buffer) {
    syscall(SYS_READ_STRING, stringId, U32(buffer), 0, 0);
}

void discardString(uintptr_t stringId) {
    syscall(SYS_DISCARD_STRING, stringId, 0, 0, 0);
}

void *memset(void *_target, int _byte, size_t size) {
    uint8_t byte = (uint8_t) _byte;
    uint8_t *target = _target;
    for (uint32_t i = 0; i < size; i++) {
        *target = byte;
        target++;
    }
    return _target;
}

