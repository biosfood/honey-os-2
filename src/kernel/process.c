#include "service/elf.h"
#include <process.h>
#include <stddef.h>
#include <vfs.h>

void *copy_from_process_to_kernel(const Process *process, void *threadRead,
                                  const uint32_t bytes_to_transfer) {
    uint8_t *result = malloc(bytes_to_transfer);
    uint8_t *write = result;
    uint8_t *read = mapTemporaryA(getPhysicalAddress(
        process->memory_information.pageDirectory, threadRead));
    // just copying byte for byte here
    for (int i = 0; i < bytes_to_transfer; ++i) {
        if ((U32(read) & 0xFFF) == 0) {
            read = mapTemporaryA(getPhysicalAddress(
                process->memory_information.pageDirectory, threadRead));
        }
        *write = *read;
        write++;
        read++;
        threadRead++;
    }
    return result;
}

void copy_from_kernel_to_process(uint8_t *read, Process *process,
                                 uint8_t *threadWrite,
                                 uint32_t bytes_to_transfer) {
    uint8_t *write = mapTemporaryA(getPhysicalAddress(
        process->memory_information.pageDirectory, threadWrite));
    for (int i = 0; i < bytes_to_transfer; i++) {
        if ((U32(write) & 0xFFF) == 0) {
            write = mapTemporaryA(getPhysicalAddress(
                process->memory_information.pageDirectory,
                threadWrite));
        }
        *write = *read;
        write++;
        read++;
        threadWrite++;
    }
}

void copy_between_processes(const ProcessThread *readThread, void *from,
                            const ProcessThread *writeThread, void *to,
                            const uint32_t bytes_to_transfer) {

    char *write = getPhysicalAddress(
        writeThread->process->memory_information.pageDirectory, to);
    char *read = getPhysicalAddress(
        readThread->process->memory_information.pageDirectory, from);
    write = mapTemporaryA(write);
    read = mapTemporaryB(read);
    for (int i = 0; i < bytes_to_transfer; i++) {
        if ((U32(read) & 0xFFF) == 0 || (U32(write) & 0xFFF) == 0) {
            read = getPhysicalAddress(
                readThread->process->memory_information.pageDirectory, from);
            write = getPhysicalAddress(
                writeThread->process->memory_information.pageDirectory, to);
            write = mapTemporaryA(write);
            read = mapTemporaryB(read);
        }
        *write = *read;
        write++;
        read++;
        from++;
        to++;
    }
}

void handlePthreadCreateSyscall(ProcessThread *thread) {
    ProcessThread *new_thread = malloc(sizeof(ProcessThread));
    memset(new_thread, 0, sizeof(ProcessThread));
    new_thread->id = id_counter++;
    new_thread->process = thread->process;
    listAdd(&(thread->process->threads), new_thread);
    new_thread->function = 0;
    new_thread->esp = malloc(0x1000);
    sharePage(&new_thread->process->memory_information, new_thread->esp,
              new_thread->esp);
    new_thread->esp += 0x1000 - 0x10;
    *(void **)new_thread->esp = PTR(thread->parameters[2]);
    *(void **)(new_thread->esp + 0x4) = &runEnd;
    // TODO: here, this should behave as pthread_exit, the main() thread exit
    // must behave as exit()
    listAdd(&threads_to_process, new_thread);
    thread->returnValue = 0;
    thread->resume = true;
}

extern void *functionsStart;
extern void *functionsEnd;

ProcessThread *processLoadELF(Process *process, File *file) {
    // use this function ONLY to load the initrd/loader program(maybe also the
    // ELF loader service)!
    void *file_data = malloc(file->size);
    file->file_system->type->read(file, file_data, file->size, 0);

    ElfHeader *header = file_data;
    ProgramHeader *programHeader =
        file_data + header->programHeaderTablePosition;
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
                memcpy(file_data + programHeader->dataOffset + page, data,
                       MIN(0x1000, programHeader->segmentFileSize - page));
            }
            sharePage(&process->memory_information, data,
                      PTR(programHeader->virtualAddress + page));
        }
    end:
        programHeader = (void *)programHeader + header->programHeaderEntrySize;
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
    *(void **)thread->esp = PTR(header->entryPosition);
    *(void **)(thread->esp + 0x4) = &runEnd;
    listAdd(&threads_to_process, thread);
    free(file_data);
    return thread;
}

void handleWriteSyscall(ProcessThread *thread) {
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
    if (thread->parameters[2] == 0) {
        thread->returnValue = 0;
        thread->resume = true;
        return;
    }
    if (file_descriptor->file->type == FILE_TYPE_FIFO) {
        FiFoFile *file = (void *)file_descriptor->file;
        if (file->blockedReadingThread) {
            ProcessThread *writeThread = file->blockedReadingThread,
                          *readThread = thread;
            // there is a thread that has called read() and no data was
            // available
            uint32_t bytes_to_transfer =
                MIN(writeThread->parameters[2], readThread->parameters[2]);
            char *threadWrite = PTR(writeThread->parameters[1]);
            char *threadRead = PTR(readThread->parameters[1]);
            copy_between_processes(readThread, threadRead, writeThread,
                                   threadWrite, bytes_to_transfer);
            file->blockedReadingThread = NULL;
            writeThread->returnValue = bytes_to_transfer;
            readThread->returnValue = bytes_to_transfer;
            listAdd(&threads_to_process, writeThread);
            thread->resume = true;
            if (bytes_to_transfer < readThread->parameters[2]) {
                PipeData *entry = malloc(sizeof(PipeData));
                entry->length = thread->parameters[2] - bytes_to_transfer;
                entry->data = malloc(entry->length);
                entry->data = copy_from_process_to_kernel(
                    thread->process,
                    PTR(writeThread->parameters[1]) + bytes_to_transfer,
                    entry->length);
                listAdd(&file->queue, entry);
            }
        } else {
            PipeData *entry = malloc(sizeof(PipeData));
            entry->length = thread->parameters[2];
            entry->data = copy_from_process_to_kernel(
                thread->process, PTR(thread->parameters[1]),
                thread->parameters[2]);
            listAdd(&file->queue, entry);
            thread->resume = true;
        }
    } else {
        void *data = copy_from_process_to_kernel(
            thread->process, PTR(thread->parameters[1]), thread->parameters[2]);
        const uint32_t bytes_transfered =
            file_descriptor->file->file_system->type->write(
                file_descriptor->file, data, thread->parameters[2],
                thread->parameters[3]);
        free(data);
        thread->returnValue = bytes_transfered;
        thread->resume = true;
    }
}

void fifo_read(ProcessThread *thread, FileDescriptor *file_descriptor) {
    FiFoFile *file = (void *)file_descriptor->file;
    if (file->queue) {
        PipeData *data = listPopFirst(&file->queue);
        uint32_t bytes_to_transfer = MIN(thread->parameters[2], data->length);
        copy_from_kernel_to_process(data->data, thread->process,
                                    PTR(thread->parameters[1]),
                                    bytes_to_transfer);
        if (data->length > thread->parameters[2]) {
            // some data is left over. Just making a new entry and putting
            // it right at the beginning of the queue
            ListElement *list_element = malloc(sizeof(ListElement));
            void *old = data->data;
            void *new = malloc(data->length - thread->parameters[2]);
            memcpy(old + thread->parameters[2], new,
                   data->length - thread->parameters[2]);
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

void handleReadSyscall(ProcessThread *thread) {
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
    if (thread->parameters[2] == 0) {
        thread->returnValue = 0;
        thread->resume = true;
        return;
    }
    if (file_descriptor->file->type == FILE_TYPE_FIFO) {
        fifo_read(thread, file_descriptor);
    } else {
        // TODO: this is horribly inefficient
        void *data = malloc(thread->parameters[2]);
        uint32_t bytes_to_transfer =
            file_descriptor->file->file_system->type->read(
                file_descriptor->file, data, thread->parameters[2],
                thread->parameters[3]);
        bytes_to_transfer = MIN(bytes_to_transfer, thread->parameters[2]);
        copy_from_kernel_to_process(
            data, thread->process, PTR(thread->parameters[1]), bytes_to_transfer);
        free(data);
        thread->resume = true;
        thread->returnValue = bytes_to_transfer;
    }
}

Process *newProcess(Container *container) {
    Process *process = malloc(sizeof(Process));
    process->id = id_counter++;
    process->container = container;
    listAdd(&container->processes, process);
    process->memory_information.pageDirectory = malloc(0x1000);
    process->cr3 =
        getPhysicalAddressKernel(process->memory_information.pageDirectory);
    process->threads = NULL;
    return process;
}

void cloneMemoryInformation(PagingInfo *from, PagingInfo *to) {
    memcpy(from, to, sizeof(PagingInfo));
    to->pageDirectory = malloc(0x1000);
    memcpy(from->pageDirectory, to->pageDirectory, 0x1000);

    for (uint32_t i = 0; i < 0x1000 / 4; i++) {
        if (!from->pageDirectory[i].present) {
            continue;
        }
        PageTableEntry *entry = malloc(0x1000);
        memcpy(mapTemporaryA(ADDRESS(from->pageDirectory[i].pageTableID)),
               entry, 0x1000);
        to->pageDirectory[i].pageTableID =
            PAGE_ID(getPhysicalAddressKernel(entry));
    }
}

void handleForkSyscall(ProcessThread *thread) {
    Process *process = newProcess(thread->process->container);
    cloneMemoryInformation(&thread->process->memory_information,
                           &process->memory_information);
    process->cr3 =
        getPhysicalAddressKernel(process->memory_information.pageDirectory);
    process->threads = NULL;
    listAdd(&thread->process->container->processes, process);
    process->container = thread->process->container;
    ProcessThread *new_thread = malloc(sizeof(ProcessThread));
    memset(new_thread, 0, sizeof(ProcessThread));
    new_thread->id = id_counter++;
    new_thread->process = process;
    listAdd(&(process->threads), new_thread);
    new_thread->function = 0;

    new_thread->esp = malloc(0x1000);
    sharePage(&process->memory_information, new_thread->esp, new_thread->esp);
    // copy the stack from the old to the new thread, but then we have to
    // rewrite all the pushed ebps.
    memcpy(ADDRESS(PAGE_ID(thread->esp)), new_thread->esp, 0x1000);
    new_thread->esp += PAGE_OFFSET(thread->esp);
    uint32_t *stack = new_thread->esp + 0x1C;
    *stack = PAGE_OFFSET(*stack) + U32(ADDRESS(PAGE_ID(stack)));
    stack = PTR(*stack);

    foreach (thread->process->openFileHandles, FileDescriptor *,
             file_descriptor, {
                 FileDescriptor *new = malloc(sizeof(FileDescriptor));
                 memcpy(file_descriptor, new, sizeof(FileDescriptor));
                 listAdd(&process->openFileHandles, new);
             })
        ;

    new_thread->returnValue = 0;
    listAdd(&threads_to_process, new_thread);

    thread->returnValue = process->id;
    thread->resume = true;
}

void handleExecSyscall(ProcessThread *thread) {
    // TODO: remove old threads
    // TODO: transfer data
    // TODO: clear memory
    void *physical =
        getPhysicalAddress(thread->process->memory_information.pageDirectory,
                           PTR(thread->parameters[0]));
    uint32_t len = strlen(mapTemporaryA(physical));
    char *data = malloc(len);
    memcpy(mapTemporaryA(physical), data, len + 1);
    File *file = thread->process->container->vfs->type->getFile(
        thread->process->container->vfs, data);
    processLoadELF(thread->process, file);
}
