#include <file.h>
#include <process.h>

#include "service/elf.h"

void handlePthreadCreateSyscall(ProcessThread *thread) {
    ProcessThread *new_thread = malloc(sizeof(ProcessThread));
    memset(new_thread, 0, sizeof(ProcessThread));
    new_thread->id = id_counter++;
    new_thread->process = thread->process;
    listAdd(&(thread->process->threads), new_thread);
    new_thread->function = 0;
    new_thread->esp = malloc(0x1000);
    sharePage(&new_thread->process->memory_information, new_thread->esp, new_thread->esp);
    new_thread->esp += 0x1000 - 0x10;
    *(void **) new_thread->esp = PTR(thread->parameters[2]);
    *(void **) (new_thread->esp + 0x4) = &runEnd;
    // TODO: here, this should behave as pthread_exit, the main() thread exit must behave as exit()
    listAdd(&threads_to_process, new_thread);
    thread->returnValue = 0;
    thread->resume = true;
}

extern void *functionsStart;
extern void *functionsEnd;

ProcessThread *processLoadELF(Process *process, void *elfStart) {
    // use this function ONLY to load the initrd/loader program(maybe also the
    // ELF loader service)!
    ElfHeader *header = elfStart;
    ProgramHeader *programHeader =
            elfStart + header->programHeaderTablePosition;
    // fire load event
    // fireEvent(loadInitrdEvent, service->nameHash, 0);
    void *current = &functionsStart;
    for (uint32_t i = 0; i < 3; i++) {
        // todo: make this unwritable!
        sharePage(&process->memory_information, current, current);
        current += 0x1000;
    }
    // reserve first few pages to hopefully catch NULL pointers correctly
    reservePagesCount(&(process->memory_information), 0, 0x10);
    for (uint32_t i = 0; i < header->programHeaderEntryCount; i++) {
        for (uint32_t page = 0; page < programHeader->segmentMemorySize;
             page += 0x1000) {
            void *data = malloc(0x1000);
            if (programHeader->segmentFileSize > page) {
                memcpy(elfStart + programHeader->dataOffset + page, data,
                       MIN(0x1000, programHeader->segmentFileSize - page));
            }
            sharePage(&process->memory_information, data,
                      PTR(programHeader->virtualAddress + page));
        }
    end:
        programHeader = (void *) programHeader + header->programHeaderEntrySize;
    }
    ProcessThread *thread = malloc(sizeof(ProcessThread));
    memset(thread, 0, sizeof(ProcessThread));
    thread->id = id_counter++;
    thread->process = process;
    listAdd(&(process->threads), thread);
    thread->function = 0;
    thread->esp = malloc(0x1000);
    sharePage(&process->memory_information, thread->esp, thread->esp);
    thread->esp += 0x1000 - 0x10;
    *(void **) thread->esp = PTR(header->entryPosition);
    *(void **) (thread->esp + 0x4) = &runEnd;
    listAdd(&threads_to_process, thread);

    return thread;
}

FileDescriptor *allocateFileDescriptor(Process *process) {
    FileDescriptor *descriptor = malloc(sizeof(FileDescriptor));

    ListElement *list_element = malloc(sizeof(ListElement));
    list_element->data = descriptor;
    list_element->next = NULL;
    if (process->openFileHandles == NULL) {
        descriptor->id = 0;
        process->openFileHandles = list_element;
    } else if (((FileDescriptor *) process->openFileHandles->data)->id > 0) {
        descriptor->id = 0;
        list_element->next = process->openFileHandles;
        process->openFileHandles = list_element;
    } else {
        ListElement *previous = process->openFileHandles;
        ListElement *current = process->openFileHandles->next;
        while (current && descriptor->id > ((FileDescriptor *) current->data)->id) {
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
    File *file = NULL;
    void *filename = PTR(thread->parameters[0]);
    filename = getPhysicalAddress(thread->process->memory_information.pageDirectory, filename);
    filename = mapTemporaryA(filename);
    foreach(thread->process->container->vfs, File*, current_file, {
            if (stringEquals(filename, current_file->name)) {
            file = current_file;
            }
            });
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
    thread->returnValue = file_descriptor->id;
}

void handleWriteSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach(thread->process->openFileHandles, FileDescriptor *, descriptor, {
            if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
            }
            })
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        thread->resume = true;
        return;
    }
    if (thread->parameters[2] == 0) {
        thread->returnValue = 0;
        thread->resume = true;
        return;
    }
    if (file_descriptor->file->type == FILE_TYPE_PIPE) {
        PipeFile *file = (void *) file_descriptor->file;
        if (file->blockedReadingThread) {
            ProcessThread *writeThread = file->blockedReadingThread, *readThread = thread;
            // there is a thread that has called read() and no data was available
            uint32_t bytes_to_transfer = MIN(writeThread->parameters[2], readThread->parameters[2]);
            char *threadWrite = PTR(writeThread->parameters[1]);
            char *write = getPhysicalAddress(writeThread->process->memory_information.pageDirectory,
                                             threadWrite);
            char *threadRead = PTR(readThread->parameters[1]);
            char *read = getPhysicalAddress(readThread->process->memory_information.pageDirectory,
                                            threadRead);
            write = mapTemporaryA(write);
            read = mapTemporaryB(read);
            for (int i = 0; i < bytes_to_transfer; i++) {
                if ((U32(read) & 0xFFF) == 0 || (U32(write) & 0xFFF) == 0) {
                    read = getPhysicalAddress(readThread->process->memory_information.pageDirectory,
                                              threadRead);
                    write = getPhysicalAddress(writeThread->process->memory_information.pageDirectory,
                                               threadWrite);
                    write = mapTemporaryA(write);
                    read = mapTemporaryB(read);
                }
                *write = *read;
                write++;
                read++;
                threadRead++;
                threadWrite++;
            }
            file->blockedReadingThread = NULL;
            writeThread->returnValue = bytes_to_transfer;
            readThread->returnValue = bytes_to_transfer;
            listAdd(&threads_to_process, writeThread);
            thread->resume = true;
            if (bytes_to_transfer < readThread->parameters[2]) {
                PipeData *entry = malloc(sizeof(PipeData));
                entry->length = thread->parameters[2] - bytes_to_transfer;
                entry->data = malloc(entry->length);
                write = entry->data;
                threadRead = PTR(thread->parameters[1]) + bytes_to_transfer;
                read =
                        mapTemporaryA(getPhysicalAddress(thread->process->memory_information.pageDirectory,
                                                         threadRead));
                // just copying byte for byte here
                for (int i = 0; i < entry->length; ++i) {
                    if ((U32(read) & 0xFFF) == 0) {
                        read = mapTemporaryA(
                            getPhysicalAddress(thread->process->memory_information.pageDirectory, threadRead));
                    }
                    *write = *read;
                    write++;
                    read++;
                    threadRead++;
                }
                listAdd(&file->queue, entry);
            }
        } else {
            PipeData *entry = malloc(sizeof(PipeData));
            entry->length = thread->parameters[2];
            entry->data = malloc(entry->length);
            char *write = entry->data;
            void *threadRead = PTR(thread->parameters[1]);
            char *read =
                    mapTemporaryA(getPhysicalAddress(thread->process->memory_information.pageDirectory, threadRead));
            // just copying byte for byte here
            for (int i = 0; i < entry->length; ++i) {
                if ((U32(read) & 0xFFF) == 0) {
                    read = mapTemporaryA(
                        getPhysicalAddress(thread->process->memory_information.pageDirectory, threadRead));
                }
                *write = *read;
                write++;
                read++;
                threadRead++;
            }
            listAdd(&file->queue, entry);
            thread->resume = true;
        }
    }
}

void handleReadSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach(thread->process->openFileHandles, FileDescriptor *, descriptor, {
            if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
            }
            });
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        thread->resume = true;
        return;
    }
    if (thread->parameters[2] == 0) {
        thread->returnValue = 0;
        thread->resume = true;
        return;
    }
    if (file_descriptor->file->type == FILE_TYPE_PIPE) {
        PipeFile *file = (void *) file_descriptor->file;
        if (file->queue) {
            PipeData *data = listPopFirst(&file->queue);
            uint32_t bytes_to_transfer = MIN(thread->parameters[2], data->length);
            char *threadWrite = PTR(thread->parameters[1]);
            char *write =
                    mapTemporaryA(getPhysicalAddress(thread->process->memory_information.pageDirectory,
                                                     threadWrite));
            char *read = data->data;
            for (int i = 0; i < bytes_to_transfer; i++) {
                if ((U32(write) & 0xFFF) == 0) {
                    write =
                            mapTemporaryA(getPhysicalAddress(thread->process->memory_information.pageDirectory,
                                                             threadWrite));
                }
                *write = *read;
                write++;
                read++;
                threadWrite++;
            }
            if (data->length > thread->parameters[2]) {
                // some data is left over.
                ListElement *list_element = malloc(sizeof(ListElement));
                void *old = data->data;
                void *new = malloc(data->length - thread->parameters[2]);
                memcpy(old + thread->parameters[2], new, data->length - thread->parameters[2]);
                free(old);
                data->length -= thread->parameters[2];
                data->data = new;
                list_element->data = data;
                list_element->next = file->queue;
                file->queue = list_element;
            } else {
                free(data->data);
                free(data);
            }
            thread->returnValue = bytes_to_transfer;
            thread->resume = true;
        } else {
            file->blockedReadingThread = thread;
            thread->resume = false;
        }
    }
}
