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


#endif // STDIO_H
