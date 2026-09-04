//
// Created by lukas on 3/27/25.
//

#include <process.h>
#include <stddef.h>
#include <sys/stat.h>
#include <util.h>

bool read_integer_from_filename(char **filename, uint32_t *data) {
    char *text = *filename;
    uint32_t result = 0;
    if (text[0] == '0' && text[1] == 'x') {
        text += 2;
        while (*text && *text != '/') {
            result <<= 4;
            if (*text >= '0' && *text <= '9') {
                result += *text - '0';
            } else if (*text >= 'A' && *text <= 'F') {
                result += *text - 'A' + 10;
            } else if (*text >= 'a' && *text <= 'f') {
                result += *text - 'a' + 10;
            } else {
                return false;
            }
            text++;
        }
    } else {
        while (*text && *text != '/') {
            result *= 10;
            if (*text >= '0' && *text <= '9') {
                result += *text - '0';
            } else {
                return false;
            }
            text++;
        }
    }
    if (*text == '/') {
        text++;
    }
    *filename = text;
    *data = result;
    return true;
}

FileDescriptor *allocateFileDescriptor(Process *process) {
    FileDescriptor *descriptor = malloc(sizeof(FileDescriptor));
    memset(descriptor, 0, sizeof(FileDescriptor));

    ListElement *list_element = malloc(sizeof(ListElement));
    list_element->data = descriptor;
    list_element->next = NULL;
    if (process->openFileHandles == NULL) {
        descriptor->id = 0;
        process->openFileHandles = list_element;
    } else if (((FileDescriptor *)process->openFileHandles->data)->id > 0) {
        descriptor->id = 0;
        list_element->next = process->openFileHandles;
        process->openFileHandles = list_element;
    } else {
        ListElement *previous = process->openFileHandles;
        ListElement *current = process->openFileHandles->next;
        descriptor->id++;
        while (current &&
               descriptor->id == ((FileDescriptor *)current->data)->id) {
            previous = current;
            current = current->next;
            descriptor->id++;
        }
        previous->next = list_element;
        if (current) {
            list_element->next = current;
        } else {
            list_element->next = NULL;
        }
    }
    return descriptor;
}

void handleStatSyscall(ProcessThread *thread) {
    FileDescriptor *file_descriptor = NULL;
    foreach (thread->process->openFileHandles, FileDescriptor *, descriptor, {
        if (thread->parameters[0] == descriptor->id) {
            file_descriptor = descriptor;
        }
    })
        ;
    if (file_descriptor == NULL) {
        thread->returnValue = -1;
        listAdd(&threads_to_process, thread);
        return;
    }
    struct stat buf;
    file_descriptor->file->file_system->type->getattr(file_descriptor->file,
                                                      &buf, thread);
    copy_from_kernel_to_process(
        &buf, thread->process, PTR(thread->parameters[1]), sizeof(struct stat));
    listAdd(&threads_to_process, thread);
    thread->returnValue = 0;
}
