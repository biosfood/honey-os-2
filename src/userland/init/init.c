#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void *thread_demo(void *arg) {
    printf("hello from the thread: %p\n", arg);
    return (void*)(uintptr_t)-5;
}

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

    char *filename = "/proc/4/exe";
    int fd = open(filename, O_SYMLINK | O_RDONLY);
    struct stat stat;
    fstat(fd, &stat);
    char *data = malloc(stat.st_size);
    read(fd, data, stat.st_size);
    printf("%s: %s\n", filename, data);
    free(data);
    close(fd);

    const int messageFile = open("/dev/cpuid/0", O_RDWR);

    uint32_t cpuidMessage[4];
    read(messageFile, cpuidMessage, 16);
    close(messageFile);
    printf("Processor manufacturer id: \"%s\"\n", (char *)(void *)cpuidMessage);

    char buf;
    read(STDIN_FILENO, &buf, 1);
    pthread_t thread;
    pthread_create(&thread, NULL, thread_demo, (void*)(uintptr_t)-3);

    read(STDIN_FILENO, &buf, 1);
    void *result;
    printf("started thread\n");
    pthread_join(thread, &result);
    printf("thread result: %p\n", result);

    while (1) {
        read(STDIN_FILENO, &buf, 1);
        printf("in: %x\n", buf);
        // write(STDOUT_FILENO, &buf, 1);
    }
}