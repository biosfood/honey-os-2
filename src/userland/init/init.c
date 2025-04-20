#include <fnctl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int portFd;

#define ioOut(port, data, len)                                                 \
    {                                                                          \
        buf = data;                                                            \
        pwrite(portFd, &buf, len, port);                                       \
    }
#define ioIn(port, len)                                                        \
    ({                                                                         \
        pread(portFd, &buf, len, port);                                        \
        buf;                                                                   \
    })
#define U32(x) ((uint32_t)(uintptr_t)(x))

void parallelOut(const uint32_t data) {
    uint32_t buf;
    if (data == '\n') {
        parallelOut('\r');
    }
    while (!(ioIn(0x379, sizeof(uint8_t)) & 0x80)) {
    }
    ioOut(0x378, U32(data), sizeof(uint8_t));

    uint8_t control = ioIn(0x37A, sizeof(uint8_t));
    ioOut(0x37A, control | 1, sizeof(uint8_t));
    ioOut(0x37A, control, sizeof(uint8_t));
    while (!(ioIn(0x379, sizeof(uint8_t)) & 0x80)) {
    }
}

char buffer[1024];

int test() {
    int fd = open("/dev/serout", 0);
    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; ++i) {
            parallelOut(buffer[i]);
        }
    }
}

int main() {
    portFd = open("/kernel/port", 0);
    mkdir("/dev/", 0);
    mkfifo("/dev/serout", 0);
    const int fd = open("/dev/serout", 0);
    pthread_create(0, 0, (void *)test, 0);
    write(fd, "Hello World\n", 12);
    const int messageFile = open("/kernel/cpuid", 0);
    uint32_t *cpuidMessage = malloc(4 * 4);
    read(messageFile, cpuidMessage, 16);
    cpuidMessage[0] = cpuidMessage[1];
    cpuidMessage[1] = cpuidMessage[3];
    cpuidMessage[3] = '\n';
    write(fd, cpuidMessage, strlen((void *)cpuidMessage));
    free(cpuidMessage);
}