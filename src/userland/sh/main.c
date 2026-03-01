//
// Created by lukas on 12/25/25.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_LINE 256
#define MAX_ARGS 16

void execute_command(char *line) {
    printf("read line %s\n", line);
    return;
    char *args[MAX_ARGS];
    char *token = strtok(line, " \n");
    int i = 0;

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;

    if (args[0] == NULL) return;

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (execv(args[0], args) == -1) {
            printf("sh: %s: command not found\n", args[0]);
            exit(1);
        }
    } else if (pid > 0) {
        // Parent process: wait for child to exit
        int status;
        waitpid(pid, &status, 0);
    } else {
        printf("sh: fork failed\n");
    }
}

int main() {
    char line[MAX_LINE];

    printf("Honey-OS 2 Shell\n");
    while (1) {
        printf("$ ");
        fflush(stdout);

        if (!fgets(line, MAX_LINE, stdin)) break;
        if (line[0] == '\n') continue;
        execute_command(line);
    }
    return 0;
}