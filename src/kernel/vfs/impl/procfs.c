//
// Created by lukas on 7/20/25.
//

#include <process.h>
#include <stddef.h>
#include <vfs.h>

typedef struct {
    struct FileSystem;
    Container *container;
    File rootdir;
    File self_link;
} ProcFileSystem;

FileOperationStatus procfs_get(ProcFileSystem *fs, char *filename,
                               struct ProcessThread *thread, File **result,
                               void **scratchpad) {
    if (!thread) {
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    if (!filename[1]) {
        *result = &fs->rootdir;
        return FILE_OPERATION_DONE;
    }
    uint32_t id = 0;
    filename++;
    if (!read_integer_from_filename(&filename, &id)) {
        if (filename[0] == 's' && filename[1] == 'e' && filename[2] == 'l' && filename[3] == 'f' && !filename[4]) {
            *result = &fs->self_link;
            return FILE_OPERATION_DONE;
        }
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    Process *process = NULL;
    foreach (fs->container->processes, Process *, current_process, {
        if (current_process->id == id) {
            process = current_process;
            break;
        }
    })
        ;
    if (!process) {
        *result = NULL;
        return FILE_OPERATION_DONE;
    }
    if (stringEquals(filename, "exe")) {
        *result = (void *)&process->process_files[PROC_FILE_EXECUTABLE];
    } else if (stringEquals(filename, "signal")) {
        *result = (void *)&process->process_files[PROC_FILE_SIGNAL];
    } else {
        *result = NULL;
    }
    return FILE_OPERATION_DONE;
}

void int_to_string(uint32_t value, char* buffer) {
    char temp[11];
    char* p = temp;

    // Edge case for 0
    if (value == 0) {
        *buffer++ = '0';
        *buffer = '\0';
        return;
    }

    // Fill temp buffer backwards
    while (value > 0) {
        *p++ = (value % 10) + '0';
        value /= 10;
    }

    // Reverse into output buffer
    while (p > temp) {
        *buffer++ = *--p;
    }
    *buffer = '\0';
}

void procfs_read(ProcessFile *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_read) {
    if ((File*)file == &((ProcFileSystem*)file->file_system)->self_link) {
        char buffer[11];
        int_to_string(thread->process->id, buffer);
        memcpy(buffer, data, strlen(buffer) + 1);
        *bytes_read = strlen(buffer) + 1;
        listAdd(&threads_to_process, thread);
        return;
    }
    if (file->type == FILE_TYPE_FIFO) {
        return fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
    }
    switch (file->file_type) {
    case PROC_FILE_EXECUTABLE:
        if (offset > file->length) {
            *bytes_read = 0;
        } else {
            *bytes_read = MIN(offset + size, file->length);
            memcpy(file->data + offset, data, *bytes_read);
        }
        break;
    default:
        break;
    }
    listAdd(&threads_to_process, thread);
}

void procfs_getattr(ProcessFile *file, struct stat *stbuf, struct ProcessThread *thread) {
    stbuf->st_size = file->length;
    if ((File*)file == &((ProcFileSystem*)file->file_system)->self_link) {
        char buffer[11];
        int_to_string(thread->process->id, buffer);
        stbuf->st_size = strlen(buffer) + 1;
        return;
    }
}

void procfs_write(ProcessFile *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_written) {
    if (file->type == FILE_TYPE_FIFO) {
        fifo_write((File *)file, data, size, bytes_written, thread);
        return;
    }
}

FileSystemType procfs_type = {
    .getFile = (void *)procfs_get,
    .create = NULL,
    .getattr = (void *)procfs_getattr,
    .read = (void *)procfs_read,
    .write = (void*)procfs_write,
};

FileSystem *create_process_fs(struct Container *container) {
    ProcFileSystem *result = malloc(sizeof(ProcFileSystem));
    result->mountedInstances = NULL;
    result->name = "PROCFS";
    result->container = container;
    result->data = NULL;

    result->rootdir.data = NULL;
    result->rootdir.file_descriptors = NULL;
    result->rootdir.file_system = (void *)result;
    result->rootdir.type = FILE_TYPE_DIRECTORY;

    result->self_link.type = FILE_TYPE_SYMLINK;
    result->self_link.file_descriptors = NULL;
    result->self_link.file_system = (void*)result;
    result->self_link.data = NULL;

    result->type = &procfs_type;
    return (void *)result;
}

void initialize_proc_files(Process *process, char *exe) {
    for (uint32_t i = 0; i < PROC_FILE_MAX; i++) {
        ProcessFile *file = &process->process_files[i];
        file->data = NULL;
        file->process = process;
        file->file_descriptors = NULL;
        file->file_system = process->container->procfs;
    }
    process->process_files[PROC_FILE_EXECUTABLE].length = strlen(exe) + 1;
    process->process_files[PROC_FILE_EXECUTABLE].data = exe;
    process->process_files[PROC_FILE_EXECUTABLE].file_type =
        PROC_FILE_EXECUTABLE;
    process->process_files[PROC_FILE_EXECUTABLE].type = FILE_TYPE_SYMLINK;

    process->process_files[PROC_FILE_SIGNAL].process = process;
    process->process_files[PROC_FILE_SIGNAL].type = FILE_TYPE_FIFO;
    process->process_files[PROC_FILE_SIGNAL].length = 0;
    process->process_files[PROC_FILE_SIGNAL].data = NULL;
    process->process_files[PROC_FILE_SIGNAL].file_descriptors = NULL;
    process->process_files[PROC_FILE_SIGNAL].file_type = PROC_FILE_SIGNAL;
}
