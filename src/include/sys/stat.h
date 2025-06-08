//
// Created by lukas on 3/27/25.
//

#ifndef STAT_H
#define STAT_H

#include <sys/types.h>
#include <time.h>

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_cmin;
    blksize_t st_blksizze;
    blkcnt_t st_blk;
};

extern int mkfifo(const char * path, mode_t mode);
extern int mkdir(const char * path, mode_t mode);
extern int fstat(int fd, struct stat *restrict buf);
extern int stat(const char *restrict path, struct stat *restrict buf);
extern int lstat(const char *restrict path, struct stat *restrict buf);

#endif //STAT_H
