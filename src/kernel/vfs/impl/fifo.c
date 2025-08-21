#include <process.h>

// this file provides the standard implementation for FIFO files.
// most vfs implementations should use these functions for all FIFO files.

typedef struct {
    uint32_t length;
    void *data;
} PipeData;

void fifo_write_descriptor(FileDescriptor *descriptor, Process *process,
                           void *process_address, uint32_t size) {
    if (descriptor->fifo_data.thread) {
        uint32_t bytes_to_transfer = MIN(size, descriptor->fifo_data.len);
        if (process) {
            memcpy_proc_to_kernel(process, process_address, descriptor->fifo_data.write_data, bytes_to_transfer);
        } else {
            memcpy(process_address, descriptor->fifo_data.write_data, bytes_to_transfer);
        }
        listAdd(&threads_to_process, descriptor->fifo_data.thread);
        if (bytes_to_transfer < descriptor->fifo_data.len) {
            PipeData *entry = malloc(sizeof(PipeData));
            entry->length = size - bytes_to_transfer;
            if (process) {
                entry->data = copy_from_process_to_kernel(
                    process, PTR(process_address) + bytes_to_transfer,
                    entry->length);
            } else {
                entry->data = malloc(entry->length);
                memcpy(process_address + bytes_to_transfer, entry->data,
                       entry->length);
            }
            listAdd(&descriptor->fifo_data.queue, entry);
        }
        descriptor->fifo_data.thread = NULL;
        *descriptor->fifo_data.bytes_read = bytes_to_transfer;
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

void fifo_read(void *write_data, uint32_t len, FiFoData *fifo, ProcessThread *thread, uint32_t *bytes_read) {
    if (!fifo->queue) {
        fifo->thread = thread;
        fifo->bytes_read = bytes_read;
        fifo->len = len;
        fifo->write_data = write_data;
        return;
    }
    PipeData *data = listPopFirst(&fifo->queue);
    *bytes_read = MIN(len, data->length);
    memcpy(data->data, write_data, *bytes_read);
    if (data->length > *bytes_read) {
        // some data is left over. Just making a new entry and putting
        // it right at the beginning of the queue
        ListElement *list_element = malloc(sizeof(ListElement));
        void *old = data->data;
        void *new = malloc(data->length - len);
        memcpy(old + len, new,
               data->length - len);
        free(old);
        data->length -= len;
        data->data = new;
        list_element->data = data;
        list_element->next = fifo->queue;
        fifo->queue = list_element;
    } else {
        free(data->data);
        free(data);
    }
    listAdd(&threads_to_process, thread);
}