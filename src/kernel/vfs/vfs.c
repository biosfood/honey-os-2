//
// Created by lukas on 3/27/25.
//

#include "fnctl.h"

#include <process.h>
#include <stddef.h>
#include <sys/stat.h>

void handleCreateFileSyscall(ProcessThread *thread) {
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
            thread->process->container->vfs, "/");
    } else {
        filename[lastSlashPosition] = 0;
        folderFile = thread->process->container->vfs->type->getFile(
            thread->process->container->vfs, filename);
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
    thread->resume = true;
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
    char *filename = copy_string_from_process(thread->process, PTR(thread->parameters[0]));
    File *file = thread->process->container->vfs->type->getFile(
        thread->process->container->vfs, filename);
    thread->resume = true;
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
                thread->process->container->vfs, directoryFileName);
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
        thread->resume = true;
        return;
    }
    listRemoveValue(&thread->process->openFileHandles, file_descriptor);
    listRemoveValue(&file_descriptor->file->file_descriptors, file_descriptor);
    free(file_descriptor);
    thread->returnValue = 0;
    thread->resume = true;
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
        thread->resume = true;
        return;
    }
    struct stat buf;
    file_descriptor->file->file_system->type->getattr(file_descriptor->file,
                                                      &buf);
    copy_from_kernel_to_process(
        &buf, thread->process, PTR(thread->parameters[1]), sizeof(struct stat));
    thread->resume = true;
    thread->returnValue = 0;
}
