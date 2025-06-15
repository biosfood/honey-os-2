#include <fnctl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fnctl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
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

void main() {
    portFd = open("/kernel/port", 0);
    int fd = open("/dev/serout", 0);
    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; ++i) {
            parallelOut(buffer[i]);
        }
    }
}