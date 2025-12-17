#include "util.h"
#include <process.h>
#include <stddef.h>
#include <stdint.h>
#include <vfs.h>

Process *newProcess(Container *container, char *exe) {
    Process *process = malloc(sizeof(Process));
    process->id = id_counter++;
    process->container = container;
    listAdd(&container->processes, process);
    process->memory_information.pageDirectory = malloc(0x1000);
    process->cr3 =
        getPhysicalAddressKernel(process->memory_information.pageDirectory);
    process->threads = NULL;
    process->openFileHandles = NULL;
    process->reap_info.exited = false;
    process->reap_info.reaped = false;

    initialize_proc_files(process, exe);

    return process;
}

void handleForkSyscall(ProcessThread *thread) {
    Process *process = newProcess(
        thread->process->container,
        combineStrings(
            "", thread->process->process_files[PROC_FILE_EXECUTABLE].data));
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
    new_thread->run = true;
    new_thread->thread_pointer_gs = thread->thread_pointer_gs;

    foreach (thread->process->virtual_memory_entries, VirtualMemoryEntry *,
             original_virtual, {
                 VirtualMemoryEntry *virtual =
                     malloc(sizeof(VirtualMemoryEntry));
                 virtual->virtual = original_virtual->virtual;
                 virtual->process = process;
                 virtual->type = original_virtual->type;
                 virtual->mappings = NULL;
                 virtual->size = original_virtual->size;
                 listAdd(&process->virtual_memory_entries, virtual);

                 foreach (original_virtual->mappings, MemoryMapping *,
                          original_mapping, {
                              MemoryMapping *mapping =
                                  malloc(sizeof(MemoryMapping));
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
                 listAdd(&new->file->file_descriptors, new);
             })
        ;

    new_thread->returnValue = 0;
    listAdd(&threads_to_process, new_thread);

    thread->returnValue = process->id;
    listAdd(&threads_to_process, thread);
}

void handleExecSyscall(ProcessThread *thread) {
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
    Process *process = thread->process;
    // TODO: arguments transfer
    listClear(process->threads);
    process->threads = NULL;

    process->process_files[PROC_FILE_EXECUTABLE].data =
        combineStrings("", file_descriptor->path);
    process->process_files[PROC_FILE_EXECUTABLE].length =
        strlen(file_descriptor->path) + 1;

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
    struct stat s;
    file_descriptor->file->file_system->type->getattr(file_descriptor->file, &s,
                                                      thread);
    void *file_data = malloc(s.st_size);
    // for now, just assume the file system is RAMfs
    uint32_t bytes_read;
    file_descriptor->file->file_system->type->read(
        file_descriptor->file, file_data, s.st_size, 0, NULL, file_descriptor,
        &bytes_read);
    processLoadELF(process, file_data);
    free(file_data);
}

void reap_process(Process *process) {
    if (process->cr3 != PTR(-1)) {
        // process hasn't exited, cannot be reaped.
        return;
    }
    uint32_t bytes_written;
    fifo_write((File *)&process->process_files[PROC_FILE_STATUS],
               &process->reap_info.exit_code, 1, &bytes_written, NULL);
    for (uint32_t i = 0; i < PROC_FILE_MAX; i++) {
        // set the file of all process file descriptors to NULL, you can no
        // longer read or write to them.
    }
}

void terminate_thread(ProcessThread *thread) {
    if (thread->readyToBeJoined) {
        return;
    }
    thread->readyToBeJoined = true;
    thread->hasBeenJoined = false;
    thread->run = false;
}

void close_file_descriptor(FileDescriptor *descriptor) {
    // descriptor->file->file_system->type->close(descriptor->file);
    // TODO
    listRemoveValue(&descriptor->file->file_descriptors, descriptor);
    free(descriptor->path);
}

void process_destroy(Process *process) {
    // call this after all reaping is done
    // TODO: make sure everything gets cleared here
    listClear(process->threads);
    // finally, we can give up the process's memory
    // we can safely do this because the process itself cannot initiate this
    // function it is called after a close()
    free(process);
}

void process_exit(Process *process, int32_t return_code) {
    foreach (process->threads, ProcessThread *, thread,
             { terminate_thread(thread); })
        ;
    foreach (process->openFileHandles, FileDescriptor *, descriptor,
             { close_file_descriptor(descriptor); })
        ;
    listClear(process->openFileHandles);

    process->reap_info.exit_code = return_code;
    process->reap_info.exited = true;
    uint32_t bytes_written = 0;
    fifo_write((File *)&process->process_files[PROC_FILE_STATUS],
               &process->reap_info.exit_code, 4, &bytes_written, NULL);
    if (bytes_written) {
        process->reap_info.reaped = true;
    }
}
