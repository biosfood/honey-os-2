#include <process.h>

void *copy_from_process_to_kernel(const Process *process, void *threadRead,
                                  const uint32_t bytes_to_transfer) {
    uint8_t *result = malloc(bytes_to_transfer);
    uint8_t *write = result;
    uint8_t *read = mapTemporaryA(getPhysicalAddress(
        process->memory_information.pageDirectory, threadRead));
    // just copying byte for byte here
    for (int i = 0; i < bytes_to_transfer; ++i) {
        if ((U32(read) & 0xFFF) == 0) {
            read = mapTemporaryA(getPhysicalAddress(
                process->memory_information.pageDirectory, threadRead));
        }
        *write = *read;
        write++;
        read++;
        threadRead++;
    }
    return result;
}

void copy_from_kernel_to_process(void *read, Process *process,
                                 void *threadWrite,
                                 uint32_t bytes_to_transfer) {
    uint8_t *write = mapTemporaryA(getPhysicalAddress(
        process->memory_information.pageDirectory, threadWrite));
    for (int i = 0; i < bytes_to_transfer; i++) {
        if ((U32(write) & 0xFFF) == 0) {
            write = mapTemporaryA(getPhysicalAddress(
                process->memory_information.pageDirectory, threadWrite));
        }
        *write = *(uint8_t*)read;
        write++;
        read++;
        threadWrite++;
    }
}

void copy_between_processes(const Process *from_process, void *from,
                            const Process *to_process, void *to,
                            const uint32_t bytes_to_transfer) {

    char *write = getPhysicalAddress(
        to_process->memory_information.pageDirectory, to);
    char *read = getPhysicalAddress(
        from_process->memory_information.pageDirectory, from);
    write = mapTemporaryA(write);
    read = mapTemporaryB(read);
    for (int i = 0; i < bytes_to_transfer; i++) {
        if ((U32(read) & 0xFFF) == 0 || (U32(write) & 0xFFF) == 0) {
            read = getPhysicalAddress(
                from_process->memory_information.pageDirectory, from);
            write = getPhysicalAddress(
                to_process->memory_information.pageDirectory, to);
            write = mapTemporaryA(write);
            read = mapTemporaryB(read);
        }
        *write = *read;
        write++;
        read++;
        from++;
        to++;
    }
}

char *copy_string_from_process(const Process *process, const void *const from) {
    uint32_t len = 0;
    const uint8_t *current_from = from;
    const uint8_t *read = mapTemporaryA(getPhysicalAddress(
        process->memory_information.pageDirectory, (void *)current_from));
    while (*read) {
        if ((U32(read) & 0xFFF) == 0) {
            read = mapTemporaryA(
                getPhysicalAddress(process->memory_information.pageDirectory,
                                   (void *)current_from));
        }
        len++;
        read++;
        current_from++;
    }
    return copy_from_process_to_kernel(process, from, len + 1);
}
