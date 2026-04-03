//
// Created by lukas on 12/25/25.
//

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int in_fd_raw, in_fd_processed;
int out_fd_raw, out_fd_processed;

void *handle_output(void *_) {
    char buffer[256];
    while (1) {
        int size = read(out_fd_processed, buffer, 256);
        if (size > 0) {
            write(out_fd_raw, buffer, size);
        }
    }
}


int main() {
    in_fd_raw = open("/dev/serial/in", O_RDONLY);
    in_fd_processed = open("/dev/tty1/in", O_WRONLY);

    out_fd_raw = open("/dev/serial/out", O_WRONLY);
    out_fd_processed = open("/dev/tty1/out", O_RDONLY);

    // notify ready, send a byte
    char d = '\n';
    int write_fd = open("/dev/tty1/in", O_WRONLY);
    write(write_fd, &d, 1);
    close(write_fd);


    pthread_t output_thread;
    pthread_create(&output_thread, NULL, handle_output, NULL);

    char buffer[1024];
    char *buffer_write = buffer;
    while (1) {
        char read_byte;
        int size = read(in_fd_raw, &read_byte, 1);
        if (size <= 0) {
            continue;
        }
        if (read_byte == '\n') {
            continue;
        }
        if (read_byte == 0x0D) {
            char *data = "\r\n";
            write(out_fd_raw, data, 2);

            *buffer_write = '\n';
            buffer_write++;
            write(in_fd_processed, buffer, buffer_write - buffer);
            buffer_write = buffer;
        } else if (read_byte == '\b') {
            char *data = "\b \b";
            write(out_fd_raw, data, 3);

            buffer_write--;
        } else {
            write(out_fd_raw, &read_byte, 1);

            *buffer_write = read_byte;
            buffer_write++;
        }
    }
}