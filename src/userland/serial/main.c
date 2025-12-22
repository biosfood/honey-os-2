#include "../../../build/musl/include/fcntl.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void *readLoop(void *) {
    int irq_fd;
    int write_fd = open("/dev/serin", O_RDWR);

    int data = open("/dev/port/0x3F8", O_RDWR);
    // everything is set up, notify everyone (the init system) by sending a byte
    uint32_t buf = 0;
    write(write_fd, &buf, 1);
    while ((irq_fd = open("/dev/pic/4", O_RDONLY)) < 0);
    while (1) {
        buf = 0;
        read(data, &buf, 1);
        while (buf) {
            write(write_fd, &buf, 1);
            read(data, &buf, 1);
        }
        read(irq_fd, &buf, 1);
    }
    return NULL;
}

char buffer[1024];

void main() {
    int fd = open("/dev/serout", O_RDONLY);

    int rbr = open("/dev/port/1016", O_WRONLY);
    int ier = open("/dev/port/1017", O_WRONLY);
    int irr = open("/dev/port/1018", O_WRONLY);
    int lcr = open("/dev/port/1019", O_WRONLY);
    int mcr = open("/dev/port/1020", O_WRONLY);
    // int lsr = open("/dev/port/1021", 0);
    // int msr = open("/dev/port/1022", 0);
    // int scr = open("/dev/port/1023", 0);

    uint8_t data = 1;
    write(ier, &data, 1);
    data = 0x80;
    write(lcr, &data, 1);
    data = 3;
    write(rbr, &data, 1);
    data = 0;
    write(ier, &data, 1);
    data = 3;
    write(lcr, &data, 1);
    data = 0xC7;
    write(irr, &data, 1);
    data = 0x0B;
    write(mcr, &data, 1);

    close(ier);
    close(irr);
    close(lcr);
    close(mcr);

    pthread_t pthread;
    pthread_create(&pthread, NULL, readLoop, NULL);
    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; i++) {
            write(rbr, &buffer[i], 1);
        }
    }
}