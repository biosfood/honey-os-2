//
// Created by lukas on 3/27/25.
//

#include <process.h>
#include <stddef.h>

extern File *findFile(char *path, ListElement *mountlist);

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
        folderFile = findFile("/", thread->process->container->vfs);
    } else {
        filename[lastSlashPosition] = 0;
        folderFile = findFile(filename, thread->process->container->vfs);
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
    if (file->type == FILE_TYPE_FIFO) {
        file->data = NULL;
        file->data_ = NULL;
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
    void *filename = PTR(thread->parameters[0]);
    filename = getPhysicalAddress(
        thread->process->memory_information.pageDirectory, filename);
    filename = mapTemporaryA(filename);
    File *file = findFile(filename, thread->process->container->vfs);
    thread->resume = true;
    if (file == NULL) {
        thread->returnValue = -1;
        return;
    }
    FileDescriptor *file_descriptor = allocateFileDescriptor(thread->process);
    file_descriptor->file = file;
    file_descriptor->process = thread->process;
    file_descriptor->offset = 0;
    file_descriptor->blockedReadingThreads = NULL;
    file_descriptor->blockedWritingThreads = NULL;
    listAdd(&thread->process->openFileHandles, file_descriptor);
    thread->returnValue = file_descriptor->id;
}

