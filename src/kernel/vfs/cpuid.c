//
// Created by lukas on 6/22/25.
//
#include <vfs.h>

typedef struct {
    FileSystem;
} CpuidFileSystem;

ListElement *cpuid_files;

FileSystem cpuid_file_system;
File cpuid_root = {
    .size = 0,
    .livesInMemory = true,
    .data = NULL,
    .data_ = NULL,
    .file_system = &cpuid_file_system,
    .type = FILE_TYPE_DIRECTORY,
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
    while (*filename) {
        if (*filename < '0' || *filename > '9') {
            return NULL;
        }
        id *= 10;
        id += *filename - '0';
        filename++;
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
    file->livesInMemory = true;
    file->size = 16;
    listAdd(&cpuid_files, file);
    return file;
}

uint32_t cpuid_read(File *file, void *data, uint32_t size, uint32_t offset) {
    if (file == &cpuid_root) {
        FillDirData buf = {.data = data,
                           .size = size,
                           .offset = offset,
                           .current_offset = 0,
                           .bytes_written = 0};
        fill_dirent(&buf, ".", FILE_TYPE_DIRECTORY);
        fill_dirent(&buf, "..", FILE_TYPE_DIRECTORY);
        return buf.bytes_written;
    }
    uint32_t output[4];
    asm volatile("cpuid"
                 : "=a"(output[3]), "=b"(output[0]), "=c"(output[2]),
                   "=d"(output[1])
                 : "a"(U32(file->data)));
    memcpy(output, data, MIN(size, 16));
    return MIN(size, 16);
}

uint32_t cpuid_getattr(File *file, struct stat *buf) {
    buf->st_uid = U32(file);
    buf->st_size = file->size;
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
