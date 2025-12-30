#include <fcntl.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define OFFSET 0x20

int control_master_fd, control_slave_fd;
int data_master_fd, data_slave_fd;

int irq_fd_dec[16];
int irq_fd_hex[16];

#define PIC_READ_ISR 0x0B
uint16_t getISR() {
    uint8_t command = PIC_READ_ISR;
    uint16_t result = 0;

    write(control_master_fd, &command, 1);
    read(control_master_fd, &result, 1);

    write(control_slave_fd, &command, 1);
    read(control_slave_fd, ((void*)&result) + 1, 1);

    return result;
}

void *handler(void *param) {
    int i = (intptr_t)param;
    char filename[100];
    sprintf(filename, "/dev/interrupt/%i", OFFSET + i);
    int interrupt_fd = open(filename, O_RDONLY);

    sprintf(filename, "/dev/pic/%i", i);
    mkfifo(filename, 0);
    irq_fd_dec[i] = open(filename, O_WRONLY);

    sprintf(&filename, "/dev/pic/0x%x", i);
    mkfifo(filename, 0);
    irq_fd_hex[i] = open(filename, O_WRONLY);

    uint32_t buf;
    while (1) {
        read(interrupt_fd, &buf, 1);
        uint16_t isr = getISR();
        bool sendPic2EOI = false;
        for (uint8_t index = 0; index < 16; index++) {
            if (!(isr & (1 << index))) {
                continue;
            }
            if (index >= 8) {
                sendPic2EOI = true;
            }
            buf = '1';
            write(irq_fd_hex[index], &buf, 1);
            write(irq_fd_dec[index], &buf, 1);
        }
        if (isr) {
            buf = 0x20;
            write(control_master_fd, &buf, 1);
            if (sendPic2EOI) {
                write(control_slave_fd, &buf, 1);
            }
        }
    }
}


int main(char **argv, int argc) {
    // currently needed as there is a bug breaking free() with multitasking...
    malloc(1);
    mkdir("/dev/pic", 0);

    control_master_fd = open("/dev/port/0x20", O_RDWR);
    data_master_fd = open("/dev/port/0x21", O_RDWR);
    control_slave_fd = open("/dev/port/0xA0", O_RDWR);
    data_slave_fd = open("/dev/port/0xA1", O_RDWR);

    // unmasiking serial and PIT
    uint32_t d = ~(1 << 4 | 1 << 0);

    write(data_master_fd, &d, 1);
    d = 0xFF;
    write(data_slave_fd, &d, 1);
    d = 0x0B;
    write(control_master_fd, &d, 1);

    d = 0x20;
    write(control_master_fd, &d, 1);
    write(control_slave_fd, &d, 1);
    pthread_t threads[16];
    for (uint8_t i = 0; i < 16; i++) {
        pthread_create(&threads[i], NULL, handler, (void*)(uintptr_t)i);
    }
    // notify ready, send a byte
    d = '\n';
    int write_fd = open("/dev/tty1/in", O_WRONLY);
    write(write_fd, &d, 1);
    close(write_fd);

    // join all threads
    for (uint8_t i = 0; i < 16; i++) {
        void *result;
        pthread_join(threads[i], &result);
    }
}