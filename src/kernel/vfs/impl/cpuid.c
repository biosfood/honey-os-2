//
// Created by lukas on 6/22/25.
//
#include "process.h"

#include <vfs.h>

typedef struct {
    FileSystem;
} CpuidFileSystem;

ListElement *cpuid_files;

FileSystem cpuid_file_system;
File cpuid_root = {
    .data = NULL,
    .file_system = &cpuid_file_system,
    .type = FILE_TYPE_DIRECTORY,
    .file_descriptors = NULL,
};

File *cpuid_get(CpuidFileSystem *cpuidfs, char *filename) {
    if (filename[0] != '/') {
        return NULL;
    }
    if (!filename[1]) {
        return &cpuid_root;
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
    foreach (cpuid_files, File *, current_file, {
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
    file->file_system = &cpuid_file_system;
    file->type = FILE_TYPE_FILE;
    listAdd(&cpuid_files, file);
    return file;
}

void cpuid_read(File *file, void *data, uint32_t size, uint32_t offset,
                struct ProcessThread *thread, struct FileDescriptor *descriptor,
                uint32_t *bytes_read) {
    if (file == &cpuid_root) {
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        fill_dirent(&buf, ".", FILE_TYPE_DIRECTORY);
        fill_dirent(&buf, "..", FILE_TYPE_DIRECTORY);
        *bytes_read = buf.bytes_written;
        listAdd(&threads_to_process, thread);
        return;
    }
    uint32_t output[4];
    asm volatile("cpuid"
                 : "=a"(output[3]), "=b"(output[0]), "=c"(output[2]),
                   "=d"(output[1])
                 : "a"(U32(file->data)));
    memcpy(output, data, MIN(size, 16));
    *bytes_read = MIN(size, 16);
    listAdd(&threads_to_process, thread);
}

uint32_t cpuid_getattr(File *file, struct stat *buf) {
    buf->st_uid = U32(file);
    buf->st_size = 10;
}

FileSystemType cpuid_file_system_type = {
    .create = NULL,
    .getFile = cpuid_get,
    .read = cpuid_read,
    .write = NULL,
    .getattr = cpuid_getattr,
};

FileSystem cpuid_file_system = {
    .name = "CPUID",
    .data = NULL,
    .mountedInstances = NULL,
    .type = &cpuid_file_system_type,
};
