#include "kernelfs.h"

#include "process.h"

#include <memory.h>
#include <stddef.h>
#include <sys/stat.h>
#include <util.h>
#include <vfs.h>

static bool parse_mem_range(const char *text, uint32_t *base, uint32_t *size) {
    if (*text == '/') {
        text++;
    }
    uint32_t b = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
        while (*text && *text != '+') {
            b <<= 4;
            if (*text >= '0' && *text <= '9') {
                b += *text - '0';
            } else if (*text >= 'A' && *text <= 'F') {
                b += *text - 'A' + 10;
            } else if (*text >= 'a' && *text <= 'f') {
                b += *text - 'a' + 10;
            } else {
                return false;
            }
            text++;
        }
    } else {
        while (*text && *text != '+') {
            b *= 10;
            if (*text >= '0' && *text <= '9') {
                b += *text - '0';
            } else {
                return false;
            }
            text++;
        }
    }
    if (*text != '+') {
        return false;
    }
    text++;
    uint32_t s = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
        while (*text && *text != '/') {
            s <<= 4;
            if (*text >= '0' && *text <= '9') {
                s += *text - '0';
            } else if (*text >= 'A' && *text <= 'F') {
                s += *text - 'A' + 10;
            } else if (*text >= 'a' && *text <= 'f') {
                s += *text - 'a' + 10;
            } else {
                return false;
            }
            text++;
        }
    } else {
        while (*text && *text != '/') {
            s *= 10;
            if (*text >= '0' && *text <= '9') {
                s += *text - '0';
            } else {
                return false;
            }
            text++;
        }
    }
    if (s == 0) {
        return false;
    }
    *base = b;
    *size = s;
    return true;
}

uint32_t mem_read(KernelFsFile *file, void *data, uint32_t size,
                  uint32_t offset, struct ProcessThread *thread,
                  struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    uint32_t phys_base = 0;
    uint32_t max_size = 0xFFFFFFFF;
    if (file->data) {
        KernelFsMemRange *range = (KernelFsMemRange *)file->data;
        phys_base = range->physical_base;
        max_size = range->size;
    }
    if (offset >= max_size) {
        *bytes_read = 0;
        if (thread) {
            listAdd(&threads_to_process, thread);
        }
        return 0;
    }
    if (offset + size > max_size) {
        size = max_size - offset;
    }
    uint32_t total = 0;
    while (total < size) {
        uint32_t current_phys = phys_base + offset + total;
        uint32_t page_offset = current_phys & 0xFFF;
        uint32_t to_copy = 4096 - page_offset;
        if (to_copy > (size - total)) {
            to_copy = size - total;
        }
        void *tmp = mapTemporaryA(ADDRESS(PAGE_ID(current_phys)));
        memcpy(tmp + page_offset, (char *)data + total, to_copy);
        total += to_copy;
    }
    *bytes_read = total;
    if (thread) {
        listAdd(&threads_to_process, thread);
    }
    return total;
}

void mem_write(struct KernelFsFile *file, void *data, uint32_t size,
               uint32_t offset) {
    uint32_t phys_base = 0;
    uint32_t max_size = 0xFFFFFFFF;
    if (file->data) {
        KernelFsMemRange *range = (KernelFsMemRange *)file->data;
        phys_base = range->physical_base;
        max_size = range->size;
    }
    if (offset >= max_size) {
        return;
    }
    if (offset + size > max_size) {
        size = max_size - offset;
    }
    uint32_t total = 0;
    while (total < size) {
        uint32_t current_phys = phys_base + offset + total;
        uint32_t page_offset = current_phys & 0xFFF;
        uint32_t to_copy = 4096 - page_offset;
        if (to_copy > (size - total)) {
            to_copy = size - total;
        }
        void *tmp = mapTemporaryA(ADDRESS(PAGE_ID(current_phys)));
        memcpy((char *)data + total, tmp + page_offset, to_copy);
        total += to_copy;
    }
}

FileOperationStatus kernelfs_get(KernelFsFileSystem *file_system, char *path,
                  ProcessThread *thread, File **result, void **scratchpad) {
    if (stringEquals(path, "/")) {
        *result = (void *)&file_system->rootDir;
    } else if (stringEquals(path, "/mem")) {
        *result = (void *)&file_system->mem;
    } else if (stringStartsWith(path, "/mem/")) {
        uint32_t base = 0, size = 0;
        if (parse_mem_range(path + 4, &base, &size)) {
            KernelFsMemRange *range = malloc(sizeof(KernelFsMemRange));
            range->physical_base = base;
            range->size = size;

            KernelFsFile *range_file = malloc(sizeof(KernelFsFile));
            memset(range_file, 0, sizeof(KernelFsFile));
            range_file->file_system = (void *)file_system;
            range_file->type = FILE_TYPE_FILE;
            range_file->data = (void *)range;
            range_file->read = mem_read;
            range_file->write = mem_write;
            range_file->file_descriptors = NULL;
            *result = (File *)range_file;
        } else {
            *result = NULL;
        }
    } else if (stringEquals(path, "/null")) {
        *result = (void *)&file_system->null;
    } else if (stringEquals(path, "/pipe")) {
        File *pipe_file = malloc(sizeof(File));
        pipe_file->file_system = (void *)file_system;
        pipe_file->type = FILE_TYPE_FIFO;
        pipe_file->data = (void *)1;
        pipe_file->file_descriptors = NULL;
        *result = pipe_file;
    } else {
        *result = NULL;
    }
    return FILE_OPERATION_DONE;
}

int kernelFsCreate(File *directory, char *name, enum FileType type) {
    return -1;
}

void kernelfs_write(struct KernelFsFile *file, void *data, uint32_t size,
                    uint32_t offset, struct ProcessThread *thread,
                    struct FileDescriptor *descriptor,
                    uint32_t *bytes_written) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_write((File *)file, data, size, bytes_written, thread);
        return;
    }
    file->write(file, data, size, offset);
    *bytes_written = 0;
    listAdd(&threads_to_process, thread);
}

void kernelfs_read(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset, struct ProcessThread *thread,
                   struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
    } else {
        file->read(file, data, size, offset, thread, descriptor, bytes_read);
    }
}

uint32_t kernelfs_getattr(File *file, struct stat *stbuf, struct ProcessThread *thread) {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFREG | 0666;
    if (file->data && file->data != (void *)1) {
        KernelFsMemRange *range = (KernelFsMemRange *)file->data;
        stbuf->st_size = range->size;
    } else {
        stbuf->st_size = 0;
    }
    return 0;
}

void kernelfs_close(File *file, FileDescriptor *descriptor) {
    if (descriptor->write) {
        bool has_writer = false;
        foreach (file->file_descriptors, FileDescriptor *, fd, {
            if (fd->write) {
                has_writer = true;
                break;
            }
        });
        if (!has_writer) {
            foreach (file->file_descriptors, FileDescriptor *, fd, {
                if (fd->read && fd->fifo_data.thread) {
                    *fd->fifo_data.bytes_read = 0;
                    listAdd(&threads_to_process, fd->fifo_data.thread);
                    fd->fifo_data.thread = NULL;
                }
            });
        }
    }
    if (file->file_descriptors == NULL) {
        if (file->data == (void *)1) {
            free(file);
        } else if (file != (void *)&kernel_fs_file_system.mem &&
                   file != (void *)&kernel_fs_file_system.null &&
                   file != (void *)&kernel_fs_file_system.rootDir &&
                   file->data != NULL) {
            free(file->data);
            free(file);
        }
    }
}

FileSystemType kernelFsType = {
    .getFile = (void*)kernelfs_get,
    .create = (void*)kernelFsCreate,
    .write = (void*)kernelfs_write,
    .read = (void*)kernelfs_read,
    .getattr = (void*)kernelfs_getattr,
    .close = (void*)kernelfs_close,
};

void null_write(struct KernelFsFile *file, void *data, uint32_t size,
                    uint32_t offset) {
}

uint32_t null_read(KernelFsFile *file, void *data, uint32_t size,
                   uint32_t offset, struct ProcessThread *thread,
                   struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    listAdd(&threads_to_process, thread);
    *bytes_read = 0;
    return 0;
}

KernelFsFileSystem kernel_fs_file_system = {
    .mountedInstances = NULL,
    .name = "KERNELFS",
    .type = &kernelFsType,
    .data = NULL,
    .rootDir =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_DIRECTORY,
            .data = NULL,
            .read = NULL,
            .write = NULL,
            .file_descriptors = NULL,
        },
    .mem =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .read = mem_read,
            .write = mem_write,
            .file_descriptors = NULL,
        },
    .null =
        {
            .file_system = (void *)&kernel_fs_file_system,
            .type = FILE_TYPE_FILE,
            .data = NULL,
            .read = null_read,
            .write = null_write,
            .file_descriptors = NULL,
        },
};

FileSystem *createKernelFs() { return (void *)&kernel_fs_file_system; }
