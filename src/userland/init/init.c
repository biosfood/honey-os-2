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

void writeBulk(char *buffer) {
    while (*buffer) {
        parallelOut(*buffer, 0);
        buffer++;
    }
}

int test() {
    int fd = open("/dev/1", 0);
    char buffer = 'X';
    while (1) {
        const uint32_t count = read(fd, &buffer, 1);
        if (count == 1) {
            parallelOut(buffer, 0);
        }
    }
}

int main() {
    const int fd = open("/dev/1", 0);
    pthread_create(0, 0, (void*) test, 0);
    writeBulk("Hello World!\n");
    char *message = "This is a test\n";
    write(fd, message, 15);
}