//
// Created by lukas on 4/20/25.
//

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include "stdlib.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static inline int printf(const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    extern int _vdprintf(AllocationData, int filedes, const char *format, va_list ap);
    return _vdprintf(allocationData, STDOUT_FILENO, format, ap);
}

static inline int asprintf(char **restrict ptr, const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    extern int _vasprintf(AllocationData allocation_data, char **restrict ptr, const char *restrict format, va_list ap);
    return _vasprintf(allocationData, ptr, format, ap);
}

extern int sprintf(const char *restrict format, ...);

#endif // STDIO_H
