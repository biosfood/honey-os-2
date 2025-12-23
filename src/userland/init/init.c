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
    mkfifo("/dev/serout", 0);
    mkfifo("/dev/serin", 0);

    // reassign STDOUT
    close(STDOUT_FILENO);
    open("/dev/serout", O_WRONLY);

    // reassign STDIN
    close(STDIN_FILENO);
    open("/dev/serin", O_RDONLY);

    char *args[] = {0};
    int status;
    pid_t pid;

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

    printf("Hello World!\n");
    pid = fork();
    if (!pid) {
        execv("/bin/index-pci", NULL);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("index pci finished: %i\n", status);
    }

    uint8_t buf;
    while (1) {
        read(STDIN_FILENO, &buf, 1);
        printf("in: %x\n", buf);
        // write(STDOUT_FILENO, &buf, 1);
    }
}