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
    File self_thread_link;
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
        if (filename[0] == 's' && filename[1] == 'e' && filename[2] == 'l' &&
            filename[3] == 'f' && !filename[4]) {
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
    if (stringStartsWith(filename, "threads/")) {
        filename += 8;
        uint32_t thread_id = 0;
        if (!read_integer_from_filename(&filename, &thread_id)) {
            if (filename[0] == 's' && filename[1] == 'e' &&
                filename[2] == 'l' && filename[3] == 'f' && !filename[4]) {
                *result = &fs->self_thread_link;
                return FILE_OPERATION_DONE;
            }
            *result = NULL;
            return FILE_OPERATION_DONE;
        }
        ProcessThread *thread = NULL;
        foreach (process->threads, ProcessThread *, current_thread, {
            if (current_thread->id == thread_id) {
                thread = current_thread;
                break;
            }
        })
            ;
        if (!thread) {
            *result = NULL;
            return FILE_OPERATION_DONE;
        }
        if (stringEquals(filename, "status")) {
            *result = (void *)&thread
                          ->thread_files[THREAD_FILE_STATUS];
        } else {
            *result = NULL;
        }
        return FILE_OPERATION_DONE;
    }
    if (stringEquals(filename, "exe")) {
        *result = (void *)&process->process_files[PROC_FILE_EXECUTABLE];
    } else if (stringEquals(filename, "signal")) {
        *result = (void *)&process->process_files[PROC_FILE_SIGNAL];
    } else if (stringEquals(filename, "status")) {
        *result = (void *)&process->process_files[PROC_FILE_STATUS];
    } else if (stringEquals(filename, "threads")) {
        *result = (void *)&process->process_files[PROC_FILE_TASKS];
    } else {
        *result = NULL;
    }
    return FILE_OPERATION_DONE;
}

void int_to_string(uint32_t value, char *buffer) {
    char temp[11];
    char *p = temp;

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
    if ((File *)file == &((ProcFileSystem *)file->file_system)->self_link) {
        char buffer[11];
        int_to_string(thread->process->id, buffer);
        memcpy(buffer, data, strlen(buffer) + 1);
        *bytes_read = strlen(buffer) + 1;
        listAdd(&threads_to_process, thread);
        return;
    }
    if ((File *)file == &((ProcFileSystem *)file->file_system)->self_thread_link) {
        char buffer[11];
        int_to_string(thread->id, buffer);
        memcpy(buffer, data, strlen(buffer) + 1);
        *bytes_read = strlen(buffer) + 1;
        listAdd(&threads_to_process, thread);
        return;
    }
    if (file->file_type == PROC_FILE_STATUS) {
        if (size < 4) {
            *bytes_read = 0;
            listAdd(&threads_to_process, thread);
            return;
        }
        if (file->process->reap_info.exited) {
            memcpy(&file->process->reap_info.exit_code, data, 4);
            *bytes_read = 4;
            file->process->reap_info.reaped = true;
        } else {
            fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
        }
        return;
    }
    if (file->file_type == -THREAD_FILE_STATUS) {
        ThreadFile *thread_file = (void*)file;
        if (size < 4) {
            *bytes_read = 0;
            listAdd(&threads_to_process, thread);
            return;
        }
        if (thread_file->thread->join_info.exited) {
            memcpy(&thread_file->thread->join_info.result, data, 4);
            *bytes_read = 4;
            thread_file->thread->join_info.joined = true;
        } else {
            fifo_read(data, size, &descriptor->fifo_data, thread, bytes_read);
        }
        return;
    }
    if (file->type == FILE_TYPE_FIFO) {
        return fifo_read(data, size, &descriptor->fifo_data, thread,
                         bytes_read);
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

void procfs_getattr(ProcessFile *file, struct stat *stbuf,
                    struct ProcessThread *thread) {
    stbuf->st_size = file->length;
    if ((File *)file == &((ProcFileSystem *)file->file_system)->self_link) {
        char buffer[11];
        int_to_string(thread->process->id, buffer);
        stbuf->st_size = strlen(buffer) + 1;
        return;
    }
}

void procfs_write(ProcessFile *file, void *data, uint32_t size, uint32_t offset,
                  struct ProcessThread *thread,
                  struct FileDescriptor *descriptor, uint32_t *bytes_written) {
    if (file->file_type == PROC_FILE_STATUS && size == 4) {
        process_exit(file->process, *(int32_t *)data);
        return;
    }
    if (file->file_type == -THREAD_FILE_STATUS && size == 4) {
        ThreadFile *thread_file = (void *)file;
        thread_exit(thread_file->thread, *(void **)data);
        return;
    }
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
    .write = (void *)procfs_write,
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
    result->self_link.file_system = (void *)result;
    result->self_link.data = NULL;

    result->self_thread_link.type = FILE_TYPE_SYMLINK;
    result->self_thread_link.file_descriptors = NULL;
    result->self_thread_link.file_system = (void *)result;
    result->self_thread_link.data = NULL;

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

    process->process_files[PROC_FILE_STATUS].process = process;
    process->process_files[PROC_FILE_STATUS].type = FILE_TYPE_FIFO;
    process->process_files[PROC_FILE_STATUS].length = 0;
    process->process_files[PROC_FILE_STATUS].data = NULL;
    process->process_files[PROC_FILE_STATUS].file_descriptors = NULL;
    process->process_files[PROC_FILE_STATUS].file_type = PROC_FILE_STATUS;

    process->process_files[PROC_FILE_ROOT].process = process;
    process->process_files[PROC_FILE_ROOT].type = FILE_TYPE_DIRECTORY;
    process->process_files[PROC_FILE_ROOT].length = 0;
    process->process_files[PROC_FILE_ROOT].data = NULL;
    process->process_files[PROC_FILE_ROOT].file_descriptors = NULL;
    process->process_files[PROC_FILE_ROOT].file_type = PROC_FILE_ROOT;

    process->process_files[PROC_FILE_TASKS].process = process;
    process->process_files[PROC_FILE_TASKS].type = FILE_TYPE_DIRECTORY;
    process->process_files[PROC_FILE_TASKS].length = 0;
    process->process_files[PROC_FILE_TASKS].data = NULL;
    process->process_files[PROC_FILE_TASKS].file_descriptors = NULL;
    process->process_files[PROC_FILE_TASKS].file_type = PROC_FILE_TASKS;
}

void initialize_thread_files(ProcessThread *thread) {
    thread->thread_files[THREAD_FILE_STATUS].file_descriptors = NULL;
    thread->thread_files[THREAD_FILE_STATUS].thread = thread;
    thread->thread_files[THREAD_FILE_STATUS].type = FILE_TYPE_FIFO;
    thread->thread_files[THREAD_FILE_STATUS].length = 0;
    thread->thread_files[THREAD_FILE_STATUS].data = NULL;
    thread->thread_files[THREAD_FILE_STATUS].file_type = -THREAD_FILE_STATUS;
    thread->thread_files[THREAD_FILE_STATUS].file_system = thread->process->container->procfs;
}
