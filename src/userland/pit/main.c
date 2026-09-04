#define PIT_A 0x40
#define PIT_CONTROL 0x43

#define PIT_MASK 0xFF
#define PIT_SCALE 1193180
#define PIT_SET 0x36

#define CMD_BINARY 0x00

#define CMD_MODE0 0x00
#define CMD_MODE1 0x02
#define CMD_MODE2 0x04
#define CMD_MODE3 0x06
#define CMD_MODE4 0x08
#define CMD_MODE5 0x0a

#define CMD_RW_BOTH 0x30

#define CMD_COUNTER0 0x00
#define CMD_COUNTER2 0x80

#include <stdio.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t systemTime = 0;
bool initialized = false;

int32_t main() {
    mkdir("/dev/pic", 0);
    mkfifo("/dev/pic/time_update", 0);
    int update_fd = open("/dev/pic/time_update", O_WRONLY);
    int time_fd = open("/dev/pic/time", O_CREAT | O_WRONLY);

    uint32_t hz = 1000;

    int fd_control = open("/dev/port/0x43", O_WRONLY);
    uint8_t control_word = CMD_BINARY | CMD_MODE3 | CMD_RW_BOTH | CMD_COUNTER0;
    write(fd_control, &control_word, 1);
    close(fd_control);

    int fd_a = open("/dev/port/0x40", O_WRONLY);
    int divisor = PIT_SCALE / hz;
    write(fd_a, &divisor, 1);
    write(fd_a, ((void*)&divisor)+1, 1);


    int irq_fd;
    while ((irq_fd = open("/dev/pic/0x0", O_RDONLY)) < 0);

    // notify ready, send a byte to stdout
    write(STDOUT_FILENO, "ready\n", 6);

    uint8_t d;
    while (1) {
        read(irq_fd, &d, 1);
        systemTime++;
    }
}
