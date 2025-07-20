#include <dirent.h>
#include <fnctl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    mkdir("/dev/", 0);
    mkfifo("/dev/serout", 0);
    mkfifo("/dev/serin", 0);

    // reassign STDOUT
    close(STDOUT_FILENO);
    open("/dev/serout", 0);

    // reassign STDIN
    close(STDIN_FILENO);
    open("/dev/serin", 0);

    pid_t pid = fork();
    if (!pid) {
        execv("/bin/serial", NULL);
    }
    pid = fork();
    if (!pid) {
        execv("/bin/pic", NULL);
    }
    pid = fork();
    if (!pid) {
        execv("/bin/index-pci", NULL);
    }

    const int messageFile = open("/dev/cpuid/0", 0);

    uint32_t cpuidMessage[4];
    read(messageFile, cpuidMessage, 16);
    close(messageFile);
    printf("Hello World!\n");
    printf("Processor manufacturer id: \"%s\"\n", (char *)(void *)cpuidMessage);

    char buf;
    while (1) {
        read(STDIN_FILENO, &buf, 1);
        printf("in: %x\n", buf);
        // write(STDOUT_FILENO, &buf, 1);
    }
}