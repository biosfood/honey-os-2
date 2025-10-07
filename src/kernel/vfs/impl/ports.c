//
// Created by lukas on 6/23/25.
//

#include "process.h"

#include <stddef.h>
#include <vfs.h>

ListElement *port_files;

FileSystem port_file_system;
File port_root = {
    .data = NULL,
    .file_system = &port_file_system,
    .type = FILE_TYPE_DIRECTORY,
    .file_descriptors = NULL,
};

FileOperationStatus port_get_file(struct FileSystem *file_system, char *filename,
                                  struct ProcessThread *thread, File **result,
                                  void **scratchpad, uint32_t options) {
    if (filename[0] != '/') {
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    if (!filename[1]) {
        *result = &port_root;
        return FILE_OPERATION_DONE;
    }
    uint32_t id = 0;
    filename++;
    if (!read_integer_from_filename(&filename, &id) || *filename) {
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    File *file = NULL;
    foreach (port_files, File *, current_file, {
        if (U32(current_file->data) == id) {
            file = current_file;
        }
    })
        ;
    if (file) {
        *result = file;
        return FILE_OPERATION_DONE;
    }
    file = malloc(sizeof(File));
    file->data = PTR(id);
    file->file_system = &port_file_system;
    file->type = FILE_TYPE_FILE;
    listAdd(&port_files, file);
    *result = file;
    return FILE_OPERATION_DONE;
}

void port_read(File *file, void *data, uint32_t size, uint32_t offset,
               struct ProcessThread *thread, struct FileDescriptor *descriptor,
               uint32_t *bytes_read) {
    if (file == &port_root) {
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        fill_dirent(&buf, ".", FILE_TYPE_DIRECTORY);
        fill_dirent(&buf, "..", FILE_TYPE_DIRECTORY);
        *bytes_read = buf.bytes_written;
    }
    uint32_t result;
    uint16_t port = (uint16_t)U32(file->data);
    switch (size) {
    case 1:
        asm("in %%dx, %%al" : "=a"(result) : "d"(port));
        break;
    case 2:
        asm("in %%dx, %%ax" : "=a"(result) : "d"(port));
        break;
    case 4:
        asm("in %%dx, %%eax" : "=a"(result) : "d"(port));
        break;
    default:
        *bytes_read = 0;
        if (thread) {
            listAdd(&threads_to_process, thread);
        }
        return;
    }
    memcpy(&result, data, MIN(4, size));
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
    *bytes_read = MIN(4, size);
}

void port_write(File *file, void *data, uint32_t size, uint32_t offset,
                struct ProcessThread *thread, struct FileDescriptor *descriptor,
                uint32_t *bytes_written) {
    if (file == &port_root) {
        *bytes_written = 0;
        goto end;
    }
    uint16_t port = (uint16_t)U32(file->data);
    switch (size) {
    case 1:
        asm("out %0, %1" : : "a"(*(uint8_t *)data), "Nd"(port));
        break;
    case 2:
        asm("out %0, %1" : : "a"(*(uint16_t *)data), "Nd"(port));
        break;
    case 4:
        asm("out %0, %1" : : "a"(*(uint32_t *)data), "Nd"(port));
        break;
    default:
        *bytes_written = 0;
        goto end;
    }
    *bytes_written = size;
end:
    listAdd(&threads_to_process, thread);
}

uint32_t port_getattr(File *file, struct stat *buf) {
    buf->st_uid = U32(file);
    buf->st_size = 1;
    return sizeof(struct stat);
}

FileSystemType port_fs_type = {
    .getFile = port_get_file,
    .read = port_read,
    .write = port_write,
    .create = NULL,
    .getattr = port_getattr,
};

FileSystem port_file_system = {
    .data = NULL,
    .mountedInstances = NULL,
    .name = "PORT",
    .type = &port_fs_type,
};
