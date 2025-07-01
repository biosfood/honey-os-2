#include <fnctl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#define OFFSET 0x20

int control_master_fd, control_slave_fd;
int data_master_fd, data_slave_fd;

int irq_fd_dec[16];
int irq_fd_hex[16];

int a;

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

void handler(int i) {
    char *filename;
    asprintf(&filename, "/dev/interrupt/%i", OFFSET + i);
    int interrupt_fd = open(filename, 0);
    free(filename);

    asprintf(&filename, "/dec/pic/%i", i);
    mkfifo(filename, 0);
    irq_fd_dec[i] = open(filename, 0);
    free(filename);

    asprintf(&filename, "/dev/pic/%i", i);
    mkfifo(filename, 0);
    irq_fd_hex[i] = open(filename, 0);
    free(filename);

    printf("init %i\n", i);
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
            printf("Interrupt %i\n", index);
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
    mkdir("/dev/pic", 0);

    control_master_fd = open("/dev/port/0x20", 0);
    data_master_fd = open("/dev/port/0x21", 0);
    control_slave_fd = open("/dev/port/0xA0", 0);
    data_slave_fd = open("/dev/port/0xA1", 0);

    // only unmasking one for now...
    uint32_t d = ~(1 << 4);

    write(data_master_fd, &d, 1);
    d = 0xFF;
    write(data_slave_fd, &d, 1);
    d = 0x0B;
    write(control_master_fd, &d, 1);

    d = 0x20;
    write(control_master_fd, &d, 1);
    write(control_slave_fd, &d, 1);
    for (uint8_t i = 0; i < 16; i++) {
        pthread_create(NULL, NULL, handler, i);
    }
    // TODO: join all threads
}