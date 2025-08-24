//
// Created by lukas on 3/27/25.
//

#include "fnctl.h"

#include <process.h>
#include <stddef.h>
#include <sys/stat.h>

bool read_integer_from_filename(char **filename, uint32_t *data) {
    char *text = *filename;
    uint32_t result = 0;
    if (text[0] == '0' && text[1] == 'x') {
        text += 2;
        while (*text && *text != '/') {
            result <<= 4;
            if (*text >= '0' && *text <= '9') {
                result += *text - '0';
            } else if (*text >= 'A' && *text <= 'F') {
                result += *text - 'A' + 10;
            } else if (*text >= 'a' && *text <= 'f') {
                result += *text - 'a' + 10;
            } else {
                return false;
            }
            text++;
        }
    } else {
        while (*text && *text != '/') {
            result *= 10;
            if (*text >= '0' && *text <= '9') {
                result += *text - '0';
            } else {
                return false;
            }
            text++;
        }
    }
    if (*text == '/') {
        text++;
    }
    *filename = text;
    *data = result;
    return true;
}

void handleCreateFileSyscall(ProcessThread *thread) {
    char *filename =
        copy_string_from_process(thread->process, PTR(thread->parameters[0]));

    uint8_t length = strlen(filename);
    if (length < 2) {
        thread->returnValue = -1;
        goto end;
    }
    if (filename[length - 1] == '/') {
        filename[strlen(filename) - 1] = 0;
        length--;
    }
    uint8_t lastSlashPosition = length - 1;
    while (filename[lastSlashPosition] != '/') {
        lastSlashPosition--;
    }
    // TODO: handle no slashes here
    File *folderFile = NULL;
    if (lastSlashPosition == 0) {
        folderFile = thread->process->container->vfs->type->getFile(
            thread->process->container->vfs, "/", thread);
    } else {
        filename[lastSlashPosition] = 0;
        folderFile = thread->process->container->vfs->type->getFile(
            thread->process->container->vfs, filename, thread);
        filename[lastSlashPosition] = '/';
    }
    if (!folderFile) {
        thread->returnValue = -1;
        goto end;
    }
    File *file = folderFile->file_system->type->create(
        folderFile, filename + lastSlashPosition + 1, thread->parameters[1]);
    if (!file) {
        thread->returnValue = -1;
        goto end;
    }
    thread->returnValue = 0;
end:
    listAdd(&threads_to_process, thread);
}

FileDescriptor *allocateFileDescriptor(Process *process) {
    FileDescriptor *descriptor = malloc(sizeof(FileDescriptor));
    descriptor->id = 0;

    ListElement *list_element = malloc(sizeof(ListElement));
    list_element->data = descriptor;
    list_element->next = NULL;
    if (process->openFileHandles == NULL) {
        descriptor->id = 0;
        process->openFileHandles = list_element;
    } else if (((FileDescriptor *)process->openFileHandles->data)->id > 0) {
        descriptor->id = 0;
        list_element->next = process->openFileHandles;
        process->openFileHandles = list_element;
    } else {
        ListElement *previous = process->openFileHandles;
        ListElement *current = process->openFileHandles->next;
        descriptor->id++;
        while (current &&
               descriptor->id == ((FileDescriptor *)current->data)->id) {
            previous = current;
            current = current->next;
            descriptor->id++;
        }
        previous->next = list_element;
        if (current) {
            list_element->next = current;
        } else {
            list_element->next = NULL;
        }
    }
    return descriptor;
}

void handleOpenSyscall(ProcessThread *thread) {
    char *filename =
        copy_string_from_process(thread->process, PTR(thread->parameters[0]));
    File *file = thread->process->container->vfs->type->getFile(
        thread->process->container->vfs, filename, thread);
    listAdd(&threads_to_process, thread);
    if (file == NULL) {
        if (thread->parameters[1] & O_CREAT) {
            char *directoryFileName = malloc(strlen(filename));
            memcpy(filename, directoryFileName, strlen(filename) + 1);
            int lastSlashPosition = strlen(filename) - 1;
            while (lastSlashPosition &&
                   directoryFileName[lastSlashPosition] != '/') {
                lastSlashPosition--;
            }
            directoryFileName[lastSlashPosition] = 0;
            File *dir = thread->process->container->vfs->type->getFile(
                thread->process->container->vfs, directoryFileName, thread);
            if (dir->type != FILE_TYPE_DIRECTORY) {
                thread->returnValue = -1;
                return;
            }
            file = dir->file_system->type->create(
                dir, filename + lastSlashPosition + 1, FILE_TYPE_FILE);
        } else {
            thread->returnValue = -1;
            return;
        }
    }
    free(filename);
    FileDescriptor *file_descriptor = allocateFileDescriptor(thread->process);
    file_descriptor->file = file;
    file_descriptor->process = thread->process;
    file_descriptor->offset = 0;
    thread->returnValue = file_descriptor->id;
    listAdd(&file->file_descriptors, file_descriptor);
}

void handleCloseSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
        if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
        }
    })
        ;
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        listAdd(&threads_to_process, thread);
        return;
    }
    listRemoveValue(&thread->process->openFileHandles, file_descriptor);
    listRemoveValue(&file_descriptor->file->file_descriptors, file_descriptor);
    free(file_descriptor);
    thread->returnValue = 0;
    listAdd(&threads_to_process, thread);
}

void handleStatSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
        if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
        }
    })
        ;
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        listAdd(&threads_to_process, thread);
        return;
    }
    struct stat buf;
    file_descriptor->file->file_system->type->getattr(file_descriptor->file,
                                                      &buf);
    copy_from_kernel_to_process(
        &buf, thread->process, PTR(thread->parameters[1]), sizeof(struct stat));
    listAdd(&threads_to_process, thread);
    thread->returnValue = 0;
}
