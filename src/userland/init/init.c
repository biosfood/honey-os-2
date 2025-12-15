#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/wait.h>

int main() {
    mkdir("/dev", 0);
    mkfifo("/dev/serout", 0);
    mkfifo("/dev/serin", 0);

    // reassign STDOUT
    close(STDOUT_FILENO);
    open("/dev/serout", 0);

    // reassign STDIN
    close(STDIN_FILENO);
    open("/dev/serin", 0);

    char *args[] = {0};
    if (!fork()) {
        execv("/bin/serial", args);
    }
    if (!fork()) {
        execv("/bin/pic", args);
    }
    if (!fork()) {
        execv("/bin/index-pci", NULL);
    }

    printf("Hello World!\n");
    char *filename = "/proc/4/exe";
    int fd = open(filename, O_SYMLINK);
    struct stat stat;
    fstat(fd, &stat);
    char *data = malloc(stat.st_size);
    read(fd, data, stat.st_size);
    printf("%s: %s\n", filename, data);
    free(data);
    close(fd);

    const int messageFile = open("/dev/cpuid/0", 0);

    uint32_t cpuidMessage[4];
    read(messageFile, cpuidMessage, 16);
    close(messageFile);
    printf("Processor manufacturer id: \"%s\"\n", (char *)(void *)cpuidMessage);

    int status;
    pid_t pid = fork();
    if (!pid) {
        exit(-5);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("finished waiting: %i, %i\n", pid, status);
    }


    char buf;
    while (1) {
        read(STDIN_FILENO, &buf, 1);
        printf("in: %x\n", buf);
        // write(STDOUT_FILENO, &buf, 1);
    }
}