#include <process.h>
#include <stddef.h>

// this file provides the standard implementation for FIFO files.
// most vfs implementations should use these functions for all FIFO files.

typedef struct {
    uint32_t length;
    void *data;
} PipeData;

void fifo_write_descriptor(FiFoData *fifo_data, void *write_data,
                           uint32_t len) {
    if (fifo_data->thread) {
        uint32_t bytes_to_transfer = MIN(len, fifo_data->len);
        memcpy(write_data, fifo_data->write_data, bytes_to_transfer);
        listAdd(&threads_to_process, fifo_data->thread);
        if (bytes_to_transfer < len) {
            PipeData *entry = malloc(sizeof(PipeData));
            entry->length = len - bytes_to_transfer;
            entry->data = malloc(entry->length);
            memcpy(write_data + bytes_to_transfer, entry->data, entry->length);
            listAdd(&fifo_data->queue, entry);
        }
        fifo_data->thread = NULL;
        *fifo_data->bytes_read = bytes_to_transfer;
    } else {
        PipeData *entry = malloc(sizeof(PipeData));
        entry->length = len;
        entry->data = malloc(len);
        memcpy(write_data, entry->data, len);
        listAdd(&fifo_data->queue, entry);
    }
}

void fifo_write(File *file, void *write_data, uint32_t len,
                uint32_t *bytes_written, ProcessThread *thread) {
    foreach (file->file_descriptors, FileDescriptor *, file_descriptor, {
        if (file_descriptor->read) {
            fifo_write_descriptor(&file_descriptor->fifo_data, write_data, len);
            *bytes_written = len;
        }
    })
        ;
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
}

void fifo_read(void *write_data, uint32_t len, FiFoData *fifo,
               ProcessThread *thread, uint32_t *bytes_read) {
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
        memcpy(old + len, new, data->length - len);
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
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
}