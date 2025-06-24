#include <process.h>

typedef struct {
    uint32_t length;
    void *data;
} PipeData;

void fifo_write_descriptor(FileDescriptor *descriptor, Process *process,
                           void *process_address, uint32_t size) {
    ProcessThread *thread = descriptor->fifo_data.blocked_reading_thread;
    if (thread) {
        uint32_t bytes_to_transfer = MIN(size, thread->parameters[2]);
        char *threadWrite = PTR(thread->parameters[1]);
        if (process) {
            copy_between_processes(process, process_address, descriptor->process,
                                   threadWrite, bytes_to_transfer);
        } else {
            copy_from_kernel_to_process(process_address, thread->process, threadWrite, bytes_to_transfer);
        }
        descriptor->fifo_data.blocked_reading_thread = NULL;
        thread->returnValue = bytes_to_transfer;
        listAdd(&threads_to_process, thread);
        thread->resume = true;
        if (bytes_to_transfer < thread->parameters[2]) {
            PipeData *entry = malloc(sizeof(PipeData));
            entry->length = size - bytes_to_transfer;
            if (process) {
                entry->data = copy_from_process_to_kernel(
                    process, PTR(process_address) + bytes_to_transfer,
                    entry->length);
            } else {
                entry->data = malloc(entry->length);
                memcpy(process_address + bytes_to_transfer, entry->data, entry->length);
            }
            listAdd(&descriptor->fifo_data.queue, entry);
        }
    } else {
        PipeData *entry = malloc(sizeof(PipeData));
        entry->length = size;
        if (process) {
            entry->data =
                copy_from_process_to_kernel(process, process_address, size);
        } else {
            entry->data = malloc(size);
            memcpy(process_address, entry->data, size);
        }
        listAdd(&descriptor->fifo_data.queue, entry);
    }
}

void fifo_write(File *file, Process *process, void *process_address,
                uint32_t size) {
    foreach (file->file_descriptors, FileDescriptor *, file_descriptor, {
        fifo_write_descriptor(file_descriptor, process, process_address, size);
    })
        ;
}

void fifo_read(ProcessThread *thread, FiFoData *fifo) {
    if (fifo->queue) {
        PipeData *data = listPopFirst(&fifo->queue);
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
            list_element->next = fifo->queue;
            fifo->queue = list_element;
        } else {
            free(data->data);
            free(data);
        }
        thread->returnValue = bytes_to_transfer;
        thread->resume = true;
    } else {
        fifo->blocked_reading_thread = thread;
        thread->resume = false;
    }
}