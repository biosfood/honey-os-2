#include "tar.h"

#include <memory.h>
#include <unistd.h>
#include <util.h>
#include <vfs.h>

uint32_t readOctal(char *string) {
    uint32_t result = 0;
    while (*string) {
        result <<= 3;
        result += *string - '0';
        string++;
    }
    return result;
}

void processInitrd(void *fileData, uint32_t tarFileSize,
                   FileSystem *file_system) {
    void *currentPosition = fileData;
    uint8_t end_count = 0;
    while (currentPosition <= fileData + tarFileSize) {
        TarFileHeader *header = currentPosition;
        uint8_t length = strlen(header->fileName);
        if (!length) {
            end_count++;
            if (end_count == 2) {
                return;
            }
            goto end;
        }
        end_count = 0;
        if (header->fileType == '5') {
            // directory
            if (length < 2) {
                goto end;
            }
            if (header->fileName[length - 1] == '/') {
                header->fileName[strlen(header->fileName) - 1] = 0;
                length--;
            }
            uint8_t lastSlashPosition = length - 1;
            while (header->fileName[lastSlashPosition] != '/') {
                lastSlashPosition--;
            }
            // TODO: handle no slashes here
            File *folderFile = NULL;
            void *scratchpad = NULL;
            if (lastSlashPosition == 0) {
                file_system->type->getFile(file_system, "/", NULL, &folderFile,
                                           &scratchpad);
            } else {
                header->fileName[lastSlashPosition] = 0;

                file_system->type->getFile(file_system, header->fileName, NULL,
                                           &folderFile, &scratchpad);
                header->fileName[lastSlashPosition] = '/';
            }
            if (folderFile->type != FILE_TYPE_DIRECTORY) {
                goto end;
            }
            file_system->type->create(folderFile,
                                      header->fileName + lastSlashPosition + 1,
                                      FILE_TYPE_DIRECTORY);
        }
        if (header->fileType == '0' || header->fileType == 0) {
            // ordinary file
            uint32_t fileSize = readOctal(header->fileSize);
            char *filename = header->fileName;
            uint32_t offset = 0;
            while (filename[offset]) {
                offset++;
                if (filename[offset] == '/') {
                    filename = filename + offset + 1;
                    offset = 0;
                }
            }
            uint8_t lastSlashPosition = length - 1;
            while (header->fileName[lastSlashPosition] != '/') {
                lastSlashPosition--;
            }
            // TODO: handle no slashes here
            File *folderFile = NULL;
            void *scratchpad = NULL;
            if (lastSlashPosition == 0) {
                file_system->type->getFile(file_system, "/", NULL, &folderFile,
                                           &scratchpad);
            } else {
                header->fileName[lastSlashPosition] = 0;
                file_system->type->getFile(file_system, header->fileName, NULL,
                                           &folderFile, &scratchpad);
                header->fileName[lastSlashPosition] = '/';
            }
            if (folderFile->type != FILE_TYPE_DIRECTORY) {
                goto end;
            }
            File *file = file_system->type->create(
                folderFile, header->fileName + lastSlashPosition + 1,
                FILE_TYPE_FILE);
            uint32_t bytes_written;
            file_system->type->write(file, currentPosition + 512, fileSize, 0,
                                     NULL, NULL, &bytes_written);
            currentPosition += 512 + ((fileSize / 512) + 1) * 512;
            continue;
        }
    end:
        currentPosition += 512;
    }
}
