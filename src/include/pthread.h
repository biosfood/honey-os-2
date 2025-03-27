//
// Created by lukas on 3/27/25.
//

#ifndef PTHREAD_H
#define PTHREAD_H

#include <sys/types.h>

extern int pthread_create(pthread_t *restrict, const pthread_attr_t *restrict,
                          void *(*)(void *), void *restrict);

#endif //PTHREAD_H
