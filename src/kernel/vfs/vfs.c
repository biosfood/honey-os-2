//
// Created by lukas on 3/27/25.
//

#include <file.h>
#include <process.h>

void handleMkFifoSyscall(ProcessThread *thread) {
    char *mapped_name = mapTemporaryB(
        getPhysicalAddress(thread->process->memory_information.pageDirectory,
                           PTR(thread->parameters[0])));
    uint32_t len = strlen(mapped_name);
    char *filename = malloc(len + 1);
    uint32_t position = 0;
    while (mapped_name[position]) {
        filename[position] = mapped_name[position];
        position++;
    }

    DirectoryFile *directory =
        findDirectoryForFile(filename, thread->process->container->vfs, true);
    char *name = filename;
    uint32_t offset = 0;
    while (name[offset]) {
        offset++;
        if (name[offset] == '/') {
            name = name + offset + 1;
            offset = 0;
        }
    }
    PipeFile *file = malloc(sizeof(PipeFile));
    file->name = malloc(strlen(name) + 1);
    memcpy(name, file->name, strlen(name) + 1);

    file->type = FILE_TYPE_PIPE;
    file->queue = NULL;
    file->blockedReadingThread = NULL;

    listAdd(&directory->children, file);
    thread->resume = true;
    thread->returnValue = 0;
}

bool nextPathPartIsDirectory(char *path) {
    while (*path) {
        if (*path == '/') {
            return true;
        }
        path++;
    }
    return false;
}

// returns the directory the file can be inserted into.
DirectoryFile *findDirectoryForFile(char *filepath, DirectoryFile *directory,
                                    bool createMissing) {
    if (!nextPathPartIsDirectory(filepath)) {
        // done!
        return directory;
    }
    DirectoryFile *found = NULL;
    foreach (directory->children, File *, file, {
        uint32_t offset = 0;
        while (file->name[offset]) {
            offset++;
        }
        if (!file->name[offset] && filepath[offset] == '/') {
            if (file->type == FILE_TYPE_DIRECTORY) {
                found = (void *)file;
            } else {
                // not a directory, have to abort
                return NULL;
            }
        }
    })
        ;
    uint32_t length = 0;
    while (filepath[length] != '/') {
        length++;
    }
    if (!length) {
        found = directory;
    } else if (!found && length == 1 && filepath[0] == '.') {
        found = directory;
    } else if (!found && createMissing) {
        found = malloc(sizeof(DirectoryFile));
        found->children = NULL;
        found->name = malloc(length + 1);
        for (int i = 0; i < length; ++i) {
            found->name[i] = filepath[i];
        }
        found->name[length] = 0;
        found->type = FILE_TYPE_DIRECTORY;
        listAdd(&directory->children, found);
    }
    return findDirectoryForFile(filepath + length + 1, found, createMissing);
}

File *findFile(char *filename, DirectoryFile *directory) {
    DirectoryFile *dir = findDirectoryForFile(filename, directory, false);
    if (!dir) {
        return NULL;
    }
    uint32_t offset = 0;
    while (filename[offset]) {
        offset++;
        if (filename[offset] == '/') {
            filename = filename + offset + 1;
            offset = 0;
        }
    }
    foreach (dir->children, File *, file, {
        if (stringEquals(file->name, filename)) {
            return file;
        }
    })
        ;
    return NULL;
}
