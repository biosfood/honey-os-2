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
        listAdd(&threads_to_process, thread);
        return;
    }
    if (thread->parameters[2] == 0) {
        thread->returnValue = 0;
        listAdd(&threads_to_process, thread);
        return;
    }
    if (file_descriptor->file->type == FILE_TYPE_FIFO) {
        fifo_write(file_descriptor->file, thread->process,
                   PTR(thread->parameters[1]), thread->parameters[2]);
        listAdd(&threads_to_process, thread);
    } else {
        void *data = copy_from_process_to_kernel(
            thread->process, PTR(thread->parameters[1]), thread->parameters[2]);
        const uint32_t bytes_transfered =
            file_descriptor->file->file_system->type->write(
                file_descriptor->file, data, thread->parameters[2],
                thread->parameters[3]);
        free(data);
        thread->returnValue = bytes_transfered;
        listAdd(&threads_to_process, thread);
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
    new_thread->run = true;

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
    file_descriptor->file->file_system->type->getattr(file_descriptor->file, &s);
    void *file_data = malloc(s.st_size);
    // for now, just assume the file system is RAMfs
    uint32_t bytes_read;
    file_descriptor->file->file_system->type->read(file_descriptor->file, file_data, s.st_size, 0, NULL, file_descriptor, &bytes_read);
    processLoadELF(process, file_data);
    free(file_data);
}
