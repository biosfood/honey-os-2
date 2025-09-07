//
// Created by lukas on 7/20/25.
//

#include <process.h>
#include <vfs.h>

typedef struct {
    FileSystem;
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
    // TODO: self: symlink to proc of current thread.
    if (!filename[1]) {
        *result = &fs->rootdir;
        return FILE_OPERATION_DONE;
    }
    uint32_t id = 0;
    filename++;
    if (!read_integer_from_filename(&filename, &id)) {
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
    } else {
        *result = NULL;
    }
    return FILE_OPERATION_DONE;
}

void procfs_read(ProcessFile *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_read) {
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

void procfs_getattr(ProcessFile *file, struct stat *stbuf) {
    stbuf->st_size = file->length;
}

FileSystemType procfs_type = {
    .getFile = procfs_get,
    .create = NULL,
    .getattr = procfs_getattr,
    .read = procfs_read,
    .write = NULL,
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
}
