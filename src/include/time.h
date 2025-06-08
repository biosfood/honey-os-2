//
// Created by lukas on 6/4/25.
//

#ifndef TIME_H
#define TIME_H

#include <sys/types.h>

typedef struct timespec {
    time_t tv_sec;
    long tv_nsec;
} timespec;


#endif //TIME_H
