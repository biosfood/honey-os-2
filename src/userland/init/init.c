#include <dirent.h>
#include <fnctl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern void initPCI();

int main() {
    struct stat stat;
    mkdir("/dev/", 0);
    mkfifo("/dev/serout", 0);

    // reassign STDOUT
    close(STDOUT_FILENO);
    open("/dev/serout", 0);

    pid_t pid = fork();
    if (!pid) {
        execv("/bin/serial", NULL);
    }

    const int messageFile = open("/dev/cpuid/0", 0);

    uint32_t cpuidMessage[4];
    read(messageFile, cpuidMessage, 16);
    close(messageFile);
    printf("Hello World!\n");
    printf("Processor manufacturer id: \"%s\"\n", (char *)(void *)cpuidMessage);

    initPCI();

    int pcidevs = open("/dev/pci", 0);
    fstat(pcidevs, &stat);
    posix_dirent *data = malloc(stat.st_size);
    int len = read(pcidevs, data, stat.st_size);
    posix_dirent *current = data;
    int classnameFiledes;
    char *classnameFilename = NULL;
    while (len) {
        if (!current->d_reclen) {
            break;
        }
        asprintf(&classnameFilename, "/dev/pci/%s/class_name", current->d_name);
        classnameFiledes = open(classnameFilename, 0);

        fstat(classnameFiledes, &stat);
        char *classname = malloc(stat.st_size);
        read(classnameFiledes, classname, stat.st_size);
        printf("%s (%i): %s\n", classnameFilename, stat.st_size, classname);
        free(classnameFilename);
        free(classname);
        close(classnameFiledes);
        len -= current->d_reclen;
        current = ((void *)current) + current->d_reclen;
    }
    free(data);
    close(pcidevs);
    printf("done\n");
    // int fd = open("/dev/interrupt/33", 0);
    int fd = open("/dev/interrupt/36", 0);
    uint32_t buf;

    int data_master_fd = open("/dev/port/0x21", 0);
    int control_master_fd = open("/dev/port/0x20", 0);
    int data_slave_fd = open("/dev/port/161", 0);
    int control_slave_fd = open("/dev/port/160", 0);
    int a = open("/dev/port/1016", 0);
    uint32_t d = ~(1 << 4);


    write(data_master_fd, &d, 1);
    d = 0xFF;
    write(data_slave_fd, &d, 1);
    // io_out(0xA1, 0x0, 1);
    // io_out(0x70, io_in(0x70, 1) | 0x80, 1);
    // io_in(0x71, 1);
    d = 0x0B;
    write(control_master_fd, &d, 1);

    d = 0x20;
    write(control_master_fd, &d, 1);
    write(control_slave_fd, &d, 1);
    read(a, &d, 1);
    while (1) {
        read(fd, &buf, 1);
        read(control_master_fd, &d, 1);
        if (d) {
            printf("interrupt: %i\n", d);
            read(a, &d, 1);
            read(a, &d, 1);
            read(a, &d, 1);
            read(a, &d, 1);
            d = 0x20;
            write(control_master_fd, &d, 1);
            write(control_slave_fd, &d, 1);
        }
    }
}