#include "tar.h"

#include <file.h>
#include <memory.h>
#include <util.h>

uint32_t readOctal(char *string) {
    uint32_t result = 0;
    while (*string) {
        result += *string - '0';
        result <<= 3;
        string++;
    }
    return result;
}

void *findTarFile(void *fileData, uint32_t tarFileSize, char *fileName) {
    void *currentPosition = fileData;
    while (currentPosition <= fileData + tarFileSize) {
        TarFileHeader *header = currentPosition;
        uint32_t fileSize = readOctal(header->fileSize);
        if (stringEquals(header->fileName, fileName)) {
            return currentPosition + 512;
        }
        currentPosition += 512;
    }
    return NULL;
}

DirectoryFile *processInitrd(void *fileData, uint32_t tarFileSize) {
    DirectoryFile *result = malloc(sizeof(DirectoryFile));
    result->children = NULL;
    result->name = "";
    result->type = FILE_TYPE_DIRECTORY;
    void *currentPosition = fileData;
    while (currentPosition <= fileData + tarFileSize) {
        TarFileHeader *header = currentPosition;
        if (header->fileType != '0' && header->fileType != 0) {
            currentPosition+=512;
            continue;
        }
        uint32_t fileSize = readOctal(header->fileSize);
        InitrdFile *file = malloc(sizeof(InitrdFile));
        char *filename = header->fileName;
        uint32_t offset = 0;
        while (filename[offset]) {
            offset++;
            if (filename[offset] == '/') {
                filename = filename + offset + 1;
                offset = 0;
            }
        }
        file->name = filename;
        file->size = readOctal(header->fileSize);
        file->kernelPosition = currentPosition + 512;
        file->size = fileSize;
        file->type = FILE_TYPE_INITRD;
        DirectoryFile *dir = findDirectoryForFile(header->fileName, result, true);
        if (!dir) {
            goto abort;
        }
        if (findFile(file->name, dir)) {
            goto abort;
        }
        listAdd(&dir->children, file);
        currentPosition += 512 + ((fileSize / 512) + 1) * 512;
        continue;
    abort:
        free(file);
        currentPosition += 512;
    }
    return result;
}

