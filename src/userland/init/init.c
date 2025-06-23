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

    printf("Hello World!\n");
    const int messageFile = open("/dev/cpuid/0", 0);
    uint32_t *cpuidMessage = malloc(4 * 4);
    read(messageFile, cpuidMessage, 16);
    close(messageFile);
    printf("Processor manufacturer id: \"%s\"\n", (char *)(void *)cpuidMessage);
    free(cpuidMessage);

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
    printf("done\n");
}