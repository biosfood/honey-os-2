#include "unistd.h"

#include <hlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

char HEX_CHARS[] = "0123456789ABCDEF";

void putHex(char **write, uintptr_t x) {
    if (x == 0) {
        **write = HEX_CHARS[x];
        (*write)++;
        **write = HEX_CHARS[x];
        (*write)++;
        return;
    }
    bool alreadyWriting = false;
    for (int position = 3; position >= 0; position--) {
        uint8_t byte = (x >> (position * 8)) & 0xFF;
        if (byte != 0x00 && !alreadyWriting) {
            alreadyWriting = true;
        }
        if (alreadyWriting) {
            **write = HEX_CHARS[byte >> 4];
            (*write)++;
            **write = HEX_CHARS[byte & 0x0F];
            (*write)++;
        }
    }
}

uint8_t hexLength(uintptr_t x) {
    bool alreadyWriting = false;
    uint8_t size = 0;
    for (int position = sizeof(uintptr_t); position >= 0; position--) {
        uint8_t byte = (x >> (position * 8)) & 0xFF;
        if (byte != 0x00 && !alreadyWriting) {
            alreadyWriting = true;
        }
        if (alreadyWriting) {
            size += 2;
        }
    }
    return MAX(size, 2);
}

uint32_t power(uintptr_t x, uintptr_t y) {
    uintptr_t result = 1;
    for (uintptr_t i = 0; i < y; i++) {
        result *= x;
    }
    return result;
}

uint32_t intLength(intptr_t x) {
    if (x == 0) {
        return 1;
    }
    for (intptr_t i = 10; i >= 0; i--) {
        if (x / power(10, i) > 0) {
            return i + 1;
        }
    }
    return 1;
}

void addChar(char **write, char c) {
    **write = c;
    (*write)++;
}

void putInt(char **write, intptr_t x) {
    if (x == 0) {
        addChar(write, '0');
        return;
    }
    if (x < 0) {
        addChar(write, '-');
        x *= -1;
    }
    for (intptr_t i = 10; i >= 0; i--) {
        uintptr_t n = x / power(10, i);
        if (n) {
            addChar(write, HEX_CHARS[n % 10]);
        }
    }
}

void putPadding(char **write, uintptr_t x) {
    x = MIN(x, 10); // max 10 wide padding
    for (intptr_t i = 0; i < x; i++) {
        addChar(write, ' ');
    }
}

uint32_t getInsertLength(char insertType, intptr_t x) {
    switch (insertType) {
    case 's':
        return strlen((char *)x);
    case 'x':
        return hexLength(x);
    case 'c':
        return 1;
    case 'i':
        return intLength(x) + (x < 0);
    case 'p':
        return x;
    }
    return 0;
}

void stringInsert(char **write, uintptr_t x) {
    char *string = (char *)x;
    uint32_t length = strlen(string);
    for (uint32_t position = 0; position < length; position++) {
        **write = string[position];
        (*write)++;
    }
}

void handleInsert(char **write, char insertType, uintptr_t x) {
    switch (insertType) {
    case 's':
        stringInsert(write, x);
        return;
    case 'x':
        putHex(write, x);
        return;
    case 'c':
        **write = x;
        (*write)++;
        return;
    case 'i':
        putInt(write, x);
        return;
    case 'p':
        putPadding(write, x);
        return;
    }
}
uint32_t ioManager, logFunction;

uint32_t printfSize(char *format, va_list ap) {
    uint32_t size = 0;
    while (*format != 0) {
        if (*format == '%') {
            char insertType = *++format;
            size += getInsertLength(insertType, va_arg(ap, uintptr_t));
        } else {
            size++;
        }
        format++;
    }
    return size;
}

void vsprintf(char *data, const char *format, va_list ap) {
    char *write = data;
    for (int i = 0; format[i] != 0; i++) {
        if (format[i] == '%') {
            handleInsert(&write, format[++i], va_arg(ap, uintptr_t));
            continue;
        }
        *write = format[i];
        write++;
    }
    *write = 0;
}


int _vasprintf(AllocationData allocation_data, char **restrict ptr, const char *restrict format, va_list ap) {
    va_list ap1, ap2;
    va_copy(ap1, ap);
    va_copy(ap2, ap);
    uint32_t length = printfSize(format, ap1);
    va_end(ap1);
    *ptr = malloc_(allocation_data, length);
    vsprintf(*ptr, format, ap2);
    va_end(ap2);
    return length;
}


int _vdprintf(AllocationData allocation_data, int filedes, const char *format, va_list ap) {
    char *data;
    int length = _vasprintf(allocation_data, &data, format, ap);
    write(filedes, data, length);
    free(data);
}


void sprintf(char *data, const char *format, ...) {
    va_list valist;
    va_start(valist, format);
    vsprintf(data, format, valist);
    va_end(valist);
}

void gets(char *buffer) {
    static uint32_t function = 0;
    if (!function) {
        function = getFunction(ioManager, "gets");
    }
    uint32_t stringId = request(ioManager, function, 0, 0);
    readString(stringId, buffer);
}
