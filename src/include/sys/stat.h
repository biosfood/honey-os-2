//
// Created by lukas on 3/27/25.
//

#ifndef STAT_H
#define STAT_H

#include <sys/types.h>

extern int mkfifo(const char * path, mode_t mode);
extern int mkdir(const char * path, mode_t mode);

#endif //STAT_H
