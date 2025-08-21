#include <dirent.h>
#include <fnctl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void readLoop() {
    int irq_fd;
    while ((irq_fd = open("/dev/pic/4", 0)) == -1);
    int write_fd = open("/dev/serin", 0);

    int data = open("/dev/port/0x3F8", 0);
    while (1) {
        uint32_t buf = 0;
        read(data, &buf, 1);
        while (buf) {
            write(write_fd, &buf, 1);
            read(data, &buf, 1);
        }
        read(irq_fd, &buf, 1);
    }
}

char buffer[1024];

void main() {
    pthread_create(NULL, NULL, readLoop, NULL);
    int fd = open("/dev/serout", 0);

    int rbr = open("/dev/port/1016", 0);
    int ier = open("/dev/port/1017", 0);
    int irr = open("/dev/port/1018", 0);
    int lcr = open("/dev/port/1019", 0);
    int mcr = open("/dev/port/1020", 0);
    int lsr = open("/dev/port/1021", 0);
    int msr = open("/dev/port/1022", 0);
    int scr = open("/dev/port/1023", 0);

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

    while (1) {
        const int32_t count = read(fd, buffer, 1024);
        for (int i = 0; i < count; i++) {
            write(rbr, &buffer[i], 1);
        }
    }
}