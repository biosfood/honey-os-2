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

int data_fd, status_fd, control_fd; // file descriptors

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
    if (data == '\n') {
        parallelOut('\r');
    }
    uint32_t status;
    do {
        read(status_fd, &status, 1);
    } while (!(status & 0x80));

    write(data_fd, (void*)&data, 1);

    uint8_t control;
    read(control_fd, &control, 1);
    control |= 1;
    write(control_fd, &control, 1);
    control &= ~1;
    write(control_fd, &control, 1);

    do {
        read(status_fd, &status, 1);
    } while (!(status & 0x80));
}

char buffer[1024];

void main() {
    data_fd = open("/dev/port/888", 0);
    status_fd = open("/dev/port/889", 0);
    control_fd = open("/dev/port/890", 0);
    int fd = open("/dev/serout", 0);
    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; ++i) {
            parallelOut(buffer[i]);
        }
    }
}