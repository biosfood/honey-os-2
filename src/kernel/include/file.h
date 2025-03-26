//
// Created by lukas on 3/26/25.
//

#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <util.h>
#include <process.h>

enum {
    FILE_TYPE_PIPE = 0,
};

typedef struct File {
    char *name;
    uint32_t type;
} File;

typedef struct {
    File *file;
    uint32_t id;
    uint32_t offset;
    Process *process;
    ListElement *blockedReadingThreads;
    ListElement *blockedWritingThreads;
} FileDescriptor;

typedef struct {
    uint32_t length;
    void *data;
} PipeData;

typedef struct {
    File;
    ListElement *queue;
    ProcessThread *blockedReadingThread;
} PipeFile;

#endif //FILE_H
