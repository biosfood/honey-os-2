#include "process.h"

#include <vfs.h>

FileSystem interrupt_file_system;

File interrupt_files[256];
File interrupt_root = {
    .file_descriptors = NULL,
    .type = FILE_TYPE_DIRECTORY,
    .data = NULL,
    .file_system = &interrupt_file_system,
};

File *interrupt_get(FileSystem *fs, char *filename) {
    if (filename[0] != '/') {
        return NULL;
    }
    if (!filename[1]) {
        return &interrupt_root;
    }
    uint32_t id = 0;
    filename++;
    if (!read_integer_from_filename(&filename, &id)) {
        return NULL;
    }
    if (*filename) {
        return NULL;
    }
    File *file = &interrupt_files[id];
    if (!file->data) {
        // uninitialized
        file->data = PTR(0xFF);
        file->file_descriptors = NULL;
        file->type = FILE_TYPE_FIFO;
        file->file_system = &interrupt_file_system;
    }
    return file;
}

int interrupt_getattr(File *file, struct stat *s) {
    if (file == &interrupt_root) {
        s->st_size = 10;
    } else {
        s->st_size = 0;
    }
}

void interrupt_read(File *file, void *data, uint32_t size,
                   uint32_t offset, struct ProcessThread *thread,
                   struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
    } else {
        // file definietly is cpuid_root
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        fill_dirent(&buf, ".", FILE_TYPE_DIRECTORY);
        fill_dirent(&buf, "..", FILE_TYPE_DIRECTORY);
        *bytes_read = buf.bytes_written;
        listAdd(&threads_to_process, thread);
    }
}

FileSystemType interrupt_file_system_type = {
    .getFile = interrupt_get,
    .getattr = interrupt_getattr,
    .create = NULL,
    .read = interrupt_read,
    .write = NULL,
};

FileSystem interrupt_file_system = {
    .name = "INTERRUPTS",
    .type = &interrupt_file_system_type,
    .data = NULL,
    .mountedInstances = NULL,
};