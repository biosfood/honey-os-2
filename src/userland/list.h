//
// Created by lukas on 9/27/25.
//

#ifndef HONEY_OS_2_LIST_H
#define HONEY_OS_2_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct ListElement {
    struct ListElement *next;
    void *data;
} ListElement;

#define foreach(list, type, varname, ...)                                      \
    for (ListElement *current = list; current; current = current->next) {      \
        type varname = current->data;                                          \
        __VA_ARGS__                                                            \
    }

inline void listAdd(ListElement **list, void *data) {
    ListElement *element = malloc(sizeof(ListElement));
    element->data = data;
    element->next = NULL;
    if (!*list) {
        *list = element;
        return;
    }
    ListElement *current = *list;
    while (current->next) {
        current = current->next;
    }
    current->next = element;
}

inline void *listPopFirst(ListElement **list) {
    if (!*list) {
        return NULL;
    }
    ListElement *resultElement = *list;
    void *result = resultElement->data;
    *list = (*list)->next;
    free(resultElement);
    return result;
}

inline uint32_t listCount(ListElement *list) {
    uint32_t i = 0;
    foreach (list, void *, element, { i++; })
        ;
    return i;
}

inline void *listGet(ListElement *list, uint32_t position) {
    for (uint32_t i = 0; i < position; i++) {
        list = list->next;
    }
    return list->data;
}

inline bool listRemoveValue(ListElement **list, void *value) {
    if (!*list) {
        return false;
    }
    ListElement *element = *list, *previous = NULL;
    while (element) {
        if (element->data == value) {
            if (previous) {
                previous->next = element->next;
            } else {
                *list = element->next;
            }
            free(element);
            return true;
        }
        previous = element;
        element = element->next;
    }
    return false;
}

inline void listClear(ListElement **list, bool freeData) {
    ListElement *current = *list;
    if (!current) {
        return;
    }
    while (current->next) {
        if (freeData) {
            free(current->data);
        }
        ListElement *next = current->next;
        free(current);
        current = next;
    }
    *list = NULL;
}

#define PTR(x) ((void *)(uintptr_t)(x))
#define U32(x) ((uint32_t)(uintptr_t)(x))

#define ADDRESS(pageId) PTR((pageId) << 12)
#define PAGE_ID(address) (U32(address) >> 12)
#define PAGE_OFFSET(address) (U32(address) & 0xFFF)

#define MIN(x, y) (x < y ? (x) : (y))
#define MAX(x, y) (x < y ? (y) : (x))

#endif // HONEY_OS_2_LIST_H
