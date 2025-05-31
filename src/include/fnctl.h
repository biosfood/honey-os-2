//
// Created by lukas on 3/27/25.
//

#ifndef FNCTL_H
#define FNCTL_H

#include <unistd.h>

extern int open(const char *, int, ...);

#define O_CLOEXEC 1
#define O_CLOFORK 2
#define O_CREAT 4
#define O_DIRECTORY 8
#define O_EXCL 16
#define O_NOCTTY 32
#define O_NOFOLLOW 64
#define O_TRUNC 128
#define O_TTY_INIT 256

#endif //FNCTL_H
