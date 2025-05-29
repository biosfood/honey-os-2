//
// Created by lukas on 5/29/25.
//

#ifndef DIRENT_H
#define DIRENT_H

#include <sys/types.h>

#define DT_BLK 1
#define DT_CHR 2
#define DT_DIR 3
#define DT_FIFO 4
#define DT_LINK 5
#define DT_REF 6
#define DT_SOCK 7
#define DT_UNKNOWN 8

typedef struct {
    ino_t d_ino;
    reclen_t d_reclen;
    unsigned char d_type;
    char d_name[];
} posix_dirent;

typedef posix_dirent dirent;

#endif //DIRENT_H
