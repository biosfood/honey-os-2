#include "elf.h"

#include <iso646.h>
#include <process.h>
#include <stddef.h>
#include <vfs.h>

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
        copy_from_kernel_to_process(data, thread->process,
                                    PTR(thread->parameters[1]),
                                    bytes_to_transfer);
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
    process->openFileHandles = NULL;
    return process;
}

void handleForkSyscall(ProcessThread *thread) {
    Process *process = newProcess(thread->process->container);
    memset(&process->memory_information, 0, sizeof(PagingInfo));
    process->memory_information.pageDirectory = malloc(0x1000);
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
    new_thread->esp = thread->esp;

    foreach (
        thread->process->virtual_memory_entries, VirtualMemoryEntry *,
        original_virtual, {
            VirtualMemoryEntry *virtual = malloc(sizeof(VirtualMemoryEntry));
            virtual->virtual = original_virtual->virtual;
            virtual->process = process;
            virtual->type = original_virtual->type;
            virtual->mappings = NULL;
            virtual->size = original_virtual->size;
            listAdd(&process->virtual_memory_entries, virtual);

            foreach (original_virtual->mappings, MemoryMapping *,
                     original_mapping, {
                         MemoryMapping *mapping = malloc(sizeof(MemoryMapping));
                         mapping->virtual = original_mapping->virtual;
                         mapping->physical = original_mapping->physical;
                         original_mapping->physical->refcount++;
                         mapping->copy_on_write = true;
                         original_mapping->copy_on_write = true;
                         listAdd(&virtual->mappings, mapping);
                     })
                ;
        })
        ;

    foreach (
        process->virtual_memory_entries, VirtualMemoryEntry *,
        virtual_memory_entry, {
            foreach (virtual_memory_entry->mappings, MemoryMapping *, mapping, {
                for (uint32_t i = 0; i < mapping->physical->page_count; i++) {
                    map(&process->memory_information,
                        mapping->physical->physical + 4096 * i,
                        mapping->virtual + 4096 * i, true)
                        ->writable = false;
                }
            })
                ;
        })
        ;
    // make old data no longer writable for the original process as well.
    foreach (
        thread->process->virtual_memory_entries, VirtualMemoryEntry *,
        virtual_memory_entry, {
            foreach (virtual_memory_entry->mappings, MemoryMapping *, mapping, {
                for (uint32_t i = 0; i < mapping->physical->page_count; i++) {
                    map(&thread->process->memory_information,
                        mapping->physical->physical + 4096 * i,
                        mapping->virtual + 4096 * i, true)
                        ->writable = false;
                }
            })
                ;
        })
        ;

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
    Process *process = thread->process;
    // TODO: arguments transfer
    char *filename =
        copy_string_from_process(process, PTR(thread->parameters[0]));
    listClear(process->threads);
    process->threads = NULL;

    foreach (process->virtual_memory_entries, VirtualMemoryEntry *, virtual, {
        foreach (virtual->mappings, MemoryMapping *, mapping, {
            unmapPageFrom(&process->memory_information,
                          mapping->physical->physical);
            mapping->physical->refcount--;
            if (mapping->physical->refcount == 0) {
                // noone is using this page anymore, so we can free it up!
                freePhysical(mapping->physical->physical,
                             mapping->physical->page_count);
                free(mapping->physical);
            }
        })
            ;
        listClear(virtual->mappings);
    })
        ;
    listClear(process->virtual_memory_entries);
    process->virtual_memory_entries = NULL;
    File *file = process->container->vfs->type->getFile(process->container->vfs,
                                                        filename);
    processLoadELF(process, file);
}
