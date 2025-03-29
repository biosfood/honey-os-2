//
// Created by lukas on 3/26/25.
//

#ifndef FILE_H
#define FILE_H

#include <process.h>
#include <stdint.h>
#include <util.h>

enum FileType {
    FILE_TYPE_PIPE = 0,
    FILE_TYPE_DIRECTORY = 1,
    FILE_TYPE_INITRD = 2,
};

typedef struct File {
    char *name;
    enum FileType type;
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

typedef struct DirectoryFile {
    File;
    ListElement *children;
} DirectoryFile;

typedef struct {
    File;
    void *kernelPosition;
    uint32_t size;
} InitrdFile;

extern DirectoryFile *findDirectoryForFile(char *filepath,
                                           DirectoryFile *directory,
                                           bool createMissing);
extern File *findFile(char *filename, DirectoryFile *directory);
extern DirectoryFile *processInitrd(void *fileData, uint32_t tarFileSize);

#endif // FILE_H
