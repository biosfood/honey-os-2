#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct Service {
    const char *name;
    const char *path;
    int stdout_pipe_read;
    int ready_pipe_read;
    int ready_pipe_write;
    pthread_t thread;
    pid_t pid;
};

static volatile int console_ready = 0;

void *supervisor_worker(void *arg) {
    struct Service *svc = (struct Service *)arg;
    char buf[256];
    char line[256];
    int line_len = 0;
    int notified = 0;

    while (1) {
        int n = read(svc->stdout_pipe_read, buf, sizeof(buf));
        if (n <= 0) {
            if (!notified) {
                notified = 1;
                char b = 0;
                write(svc->ready_pipe_write, &b, 1);
            }
            break;
        }
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[line_len] = '\0';
                if (console_ready) {
                    char log_buf[300];
                    int len = snprintf(log_buf, sizeof(log_buf), "[%-8s]: %s\r\n", svc->name, line);
                    write(STDOUT_FILENO, log_buf, len);
                }
                line_len = 0;
            } else if (buf[i] != '\r' && line_len < (int)sizeof(line) - 1) {
                line[line_len++] = buf[i];
            }
        }
        if (!notified) {
            notified = 1;
            char b = 1;
            write(svc->ready_pipe_write, &b, 1);
        }
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        if (console_ready) {
            char log_buf[300];
            int len = snprintf(log_buf, sizeof(log_buf), "[%-8s]: %s\r\n", svc->name, line);
            write(STDOUT_FILENO, log_buf, len);
        }
    }
    close(svc->stdout_pipe_read);
    close(svc->ready_pipe_write);
    return NULL;
}

void start_supervised_service(struct Service *svc) {
    int stdout_pipe[2];
    int ready_pipe[2];

    if (pipe(stdout_pipe) < 0 || pipe(ready_pipe) < 0) {
        return;
    }

    svc->stdout_pipe_read = stdout_pipe[0];
    svc->ready_pipe_read = ready_pipe[0];
    svc->ready_pipe_write = ready_pipe[1];

    pid_t pid = fork();
    if (!pid) {
        // In child: redirect stdout to stdout_pipe[1]
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(ready_pipe[0]);
        close(ready_pipe[1]);

        char *args[] = { NULL };
        execv(svc->path, args);
        exit(1);
    }

    // In parent:
    close(stdout_pipe[1]); // parent doesn't write to child stdout
    svc->pid = pid;

    // Start supervisor thread
    pthread_create(&svc->thread, NULL, supervisor_worker, svc);

    // Wait until supervisor thread signals readiness
    char sig;
    read(svc->ready_pipe_read, &sig, 1);
    close(svc->ready_pipe_read);
}

int main() {
    mkdir("/dev", 0);

    mkdir("/dev/serial", 0);
    mkfifo("/dev/serial/out", 0);
    mkfifo("/dev/serial/in", 0);

    mkdir("/dev/tty1", 0);
    mkfifo("/dev/tty1/out", 0);
    mkfifo("/dev/tty1/in", 0);

    // reassign STDOUT to tty1/out
    close(STDOUT_FILENO);
    open("/dev/tty1/out", O_WRONLY);

    // reassign STDIN to tty1/in
    close(STDIN_FILENO);
    open("/dev/tty1/in", O_RDONLY);

    // Start serial stack first so console output works
    struct Service pty_svc = { .name = "pty", .path = "/bin/pty" };
    start_supervised_service(&pty_svc);

    struct Service serial_svc = { .name = "serial", .path = "/bin/serial" };
    start_supervised_service(&serial_svc);

    // Console output path (tty1/out -> pty -> serial/out -> serial -> COM1) is active
    console_ready = 1;

    struct Service pic_svc = { .name = "pic", .path = "/bin/pic" };
    start_supervised_service(&pic_svc);

    struct Service pit_svc = { .name = "pit", .path = "/bin/pit" };
    start_supervised_service(&pit_svc);

    printf("Hello World!\r\n");
    fflush(stdout);

    char *args[] = { NULL };
    int status;
    pid_t pid;

    pid = fork();
    if (!pid) {
        execv("/bin/index-pci", args);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("index pci finished: %i\r\n", status);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/test-mmio", args);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("test-mmio finished: %i\r\n", status);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/hello-rust", args);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("hello-rust finished: %i\r\n", status);
    }

    pid = fork();
    if (!pid) {
        execv("/bin/sh", args);
    } else {
        waitpid(pid, &status, WUNTRACED);
        printf("sh finished: %i\r\n", status);
    }

    uint8_t buf[256];
    while (1) {
        int len = read(STDIN_FILENO, buf, 256);
        buf[len] = 0;
        printf("in: %s\r\n", buf);
    }
}