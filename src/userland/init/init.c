#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    malloc(1);
    mkdir("/dev", 0);

    mkdir("/dev/serial", 0);
    mkfifo("/dev/serial/out", 0);
    mkfifo("/dev/serial/in", 0);

    mkdir("/dev/tty1", 0);
    mkfifo("/dev/tty1/out", 0);
    mkfifo("/dev/tty1/in", 0);

    // reassign STDOUT
    close(STDOUT_FILENO);
    open("/dev/tty1/out", O_WRONLY);

    // reassign STDIN
    close(STDIN_FILENO);
    open("/dev/tty1/in", O_RDONLY);

    char *args[] = {0};
    int status;
    pid_t pid;

    pid = fork();
    if (!pid) {
        execv("/bin/pty", args);
    } else {
        read(STDIN_FILENO, &status, 1);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/pic", args);
    } else {
        read(STDIN_FILENO, &status, 1);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/serial", args);
    } else {
        read(STDIN_FILENO, &status, 1);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/pit", args);
    } else {
        read(STDIN_FILENO, &status, 1);
    }

    printf("Hello World!\n");
    pid = fork();
    if (!pid) {
        execv("/bin/index-pci", args);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("index pci finished: %i\n", status);
    }

    // pid = fork();
    // if (!pid) {
    //     execv("/bin/sh", args);
    // } else {
    //     waitpid(pid, &status, WUNTRACED);
    //     printf("sh finished: %i\n", status);
    // }

    uint8_t buf[256];
    while (1) {
        int len = read(STDIN_FILENO, buf, 256);
        buf[len] = 0;
        printf("in: %s\n", buf);
        // write(STDOUT_FILENO, &buf, 1);
    }
}