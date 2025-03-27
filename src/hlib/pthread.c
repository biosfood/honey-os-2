//
// Created by lukas on 3/27/25.
//
#include <hlib.h>
#include <pthread.h>
#include <syscalls.h>

int pthread_create(pthread_t *restrict thread, const pthread_attr_t *restrict attr,
                          void *(*start_routine)(void *), void *restrict arg) {
    return (int)syscall(SYS_PTHREAD_CREATE, U32(thread), U32(attr), U32(start_routine), U32(arg));
}

