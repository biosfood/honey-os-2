#include <fnctl.h>
#include <hlib.h>
#include <pthread.h>
#include <stdint.h>

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

char buffer[1024];

int test() {
    int fd = open("/dev/serout", 0);
    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; ++i) {
            parallelOut(buffer[i], 0);
        }
    }
}

int main() {
    mkfifo("/dev/serout");
    const int fd = open("/dev/serout", 0);
    pthread_create(0, 0, (void *)test, 0);
    char *message = "Hello World\n";
    write(fd, message, 15);
}