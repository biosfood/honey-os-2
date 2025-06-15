// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html

#ifndef UNISTD_H
#define UNISTD_H

#define _POSIX_VERSION 202405L
#define _POSIX2_VERSION 202405L
#define _XOPEN_VERSION 800

#include <stddef.h>
#include <sys/types.h>
// #include <fcntl.h>

extern ssize_t read(int, void *, size_t);
extern ssize_t pread(int filedes, void *buf, size_t nbyte, off_t offset);
extern ssize_t write(int, void *, size_t);
extern ssize_t pwrite(int filedes, void *buf, size_t nbyte, off_t offset);
extern int close(int fildes);
extern pid_t fork(void);

extern int execv(const char *path, char *const argv[]);

#endif //UNISTD_H
