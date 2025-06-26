//
// Created by lukas on 6/23/25.
//

#include <vfs.h>

ListElement *port_files;

FileSystem port_file_system;
File port_root = {
    .data = NULL,
    .file_system = &port_file_system,
    .type = FILE_TYPE_DIRECTORY,
    .file_descriptors = NULL,
};

File *port_get_file(FileSystem *file_system, const char *filename) {
    if (filename[0] != '/') {
        return NULL;
    }
    if (!filename[1]) {
        return &port_root;
    }
    uint32_t id = 0;
    filename++;
    if (!read_integer_from_filename(&filename, &id)) {
        return NULL;
    }
    if (*filename) {
        return NULL;
    }
    File *file = NULL;
    foreach (port_files, File *, current_file, {
        if (U32(current_file->data) == id) {
            file = current_file;
        }
    })
        ;
    if (file) {
        return file;
    }
    file = malloc(sizeof(File));
    file->data = PTR(id);
    file->file_system = &port_file_system;
    file->type = FILE_TYPE_FILE;
    listAdd(&port_files, file);
    return file;
}

uint32_t port_read(File *file, void *data, uint32_t size, uint32_t offset) {
    if (file == &port_root) {
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        fill_dirent(&buf, ".", FILE_TYPE_DIRECTORY);
        fill_dirent(&buf, "..", FILE_TYPE_DIRECTORY);
        return buf.bytes_written;
    }
    uint32_t result;
    uint16_t port = (uint16_t) U32(file->data);
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
        return 0;
    }
    memcpy(&result, data, MIN(4,size));
    return MIN(4,size);
}

uint32_t port_write(File *file, void *data, uint32_t size, uint32_t offset) {
    if (file == &port_root) {
        return 0;
    }
    uint16_t port = (uint16_t) U32(file->data);
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
        return 0;
    }
    return size;
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
