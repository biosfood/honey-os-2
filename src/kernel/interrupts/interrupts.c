#include "interrupts.h"

#include "process.h"

#include <interrupts.h>
#include <memory.h>
#include <stddef.h>
#include <syscall.h>
#include <util.h>

#define IDT_ENTRY(i)                                                           \
    idtEntries[i].offsetLow = U32(&idtHandler##i) & 0xFFFF;                    \
    idtEntries[i].offsetHigh = U32(&idtHandler##i) >> 16;

extern void *idt;
extern GDTEntry newGDT;
extern TSS tss;

ListElement *interruptSubscriptions[255];

__attribute__((section(".sharedFunctions")))
__attribute__((aligned(0x10))) IdtEntry idtEntries[256] = {};

ListElement *exceptionSubscriptions;
extern File interrupt_files[256];
extern ProcessThread *current_thread;
extern uint32_t interruptReturn;

void onInterrupt(void *cr2, void *cr3, uint32_t edi, uint32_t esi, uint32_t ebp, uint32_t _esp, uint32_t ebx, uint32_t edx, uint32_t ecx, uint32_t eax, uint32_t intNo, uint32_t errorCode,
                 uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t esp) {
    uint32_t data = '1';
    File *file = &interrupt_files[intNo];
    if (!file->data) {
        return;
    }
    uint32_t bytes_written;
    fifo_write(file, &data, 1, &bytes_written, NULL);
    return;
    // TODO: make sure there actually is enough space on the stack here....
    uint32_t newStack[] = {
        U32(&interruptReturn), edi, esi, ebp, U32(current_thread->esp) + 4, ebx, edx, ecx, eax, eip,
    };
    current_thread->esp = PTR(esp) - sizeof(newStack);
    current_thread->function = 0;
    listAdd(&threads_to_process, current_thread);
    memcpy(newStack,
           mapTemporaryA(getPhysicalAddress(
               current_thread->process->memory_information.pageDirectory,
               current_thread->esp)),
           sizeof(newStack));
}

void onException(void *cr2, void *cr3, uint32_t edi, uint32_t esi, uint32_t ebp, uint32_t _esp, uint32_t ebx, uint32_t edx, uint32_t ecx, uint32_t eax, uint32_t intNo, uint32_t errorCode,
                 uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t esp) {
    if (intNo == 0x0E && errorCode == 7) {
        // page-protection violation, write access and in usermode
        // we are probaly dealing with a copy-on-write page here.
        PhysicalMemoryEntry *physical = NULL;
        MemoryMapping *mapping = NULL;
        VirtualMemoryEntry *virtual = NULL;
        foreach (
            current_thread->process->virtual_memory_entries,
            VirtualMemoryEntry *, current_virtual, {
                if (current_virtual->virtual > cr2 ||
                    current_virtual->virtual + current_virtual->size <= cr2) {
                    continue;
                }
                foreach (
                    current_virtual->mappings, MemoryMapping *, current_mapping,
                    {
                        if (current_mapping->virtual > cr2 ||
                            current_mapping->virtual +
                                    4096 *
                                        current_mapping->physical->page_count <=
                                cr2) {
                            continue;
                        }
                        physical = current_mapping->physical;
                        mapping = current_mapping;
                        virtual = current_virtual;
                        break;
                    })
                    ;
            })
            ;
        while (!physical || !mapping || !virtual)
            ;
        if (physical->refcount == 1) {
            for (uint32_t i = 0; i < physical->page_count; i++) {
                PageTableEntry *entry =
                    map(&current_thread->process->memory_information,
                        physical->physical + 4096 * i,
                        mapping->virtual + 4096 * i, true);
                entry->writable = 1;
                entry->available = 1;
            }
            mapping->copy_on_write = false;
        } else {
            physical->refcount--;
            PhysicalMemoryEntry *new_physical =
                malloc(sizeof(PhysicalMemoryEntry));
            new_physical->physical = getPhysicalPages(physical->page_count);
            new_physical->page_count = physical->page_count;
            mapping->physical = new_physical;
            new_physical->refcount = 1;
            for (uint32_t i = 0; i < physical->page_count; i++) {
                memcpy(mapTemporaryA(physical->physical + 4096 * i),
                       mapTemporaryB(new_physical->physical + 4096 * i), 4096);
                PageTableEntry *entry =
                    map(&current_thread->process->memory_information,
                        new_physical->physical + 4096 * i,
                        mapping->virtual + 4096 * i, true);
                entry->writable = 1;
                entry->available = 1;
            }
        }
        goto resume;
    }

    while (1)
        ;

resume:
    // TODO: make sure there actually is enough space on the stack here....
    uint32_t newStack[] = {
        U32(&interruptReturn), edi, esi, ebp, 0, ebx, edx, ecx, eax, eip,
    };
    current_thread->esp = PTR(esp) - sizeof(newStack);
    current_thread->function = 0;
    listAdd(&threads_to_process, current_thread);
    memcpy(newStack,
           mapTemporaryA(getPhysicalAddress(
               current_thread->process->memory_information.pageDirectory,
               current_thread->esp)),
           sizeof(newStack));
}

extern void *interruptStack;

void setupPic();

void registerInterrupts() {
    setupPic();
    GDTEntry *currentGdt = &newGDT;
    currentGdt[5].limit = sizeof(TSS);
    currentGdt[5].baseLow = U32(&tss);
    currentGdt[5].baseMid = U32(&tss) >> 16;
    currentGdt[5].baseHigh = U32(&tss) >> 24;
    currentGdt[5].access = 0xE9;
    currentGdt[5].granularity = 0;
    currentGdt[3].access = 0xFE;
    currentGdt[4].access = 0xF2;
    tss.ss0 = tss.ss = 0x10;
    tss.esp0 = tss.esp = U32(&interruptStack) + 1024;
    asm("mov $40, %%ax" ::);
    asm("ltr %%ax" ::);
    for (uint16_t i = 0; i < 256; i++) {
        idtEntries[i].reserved = 0;
        idtEntries[i].type = 0xEE;
        idtEntries[i].segment = 0x8;
    }
    TIMES(IDT_ENTRY);
    InterruptTablePointer pointer = {
        .base = U32(&idtEntries),
        .limit = sizeof(idtEntries) - 1,
    };
    asm("lidt %0" ::"m"(pointer));
    asm("sti");
}

#define outb(port, value)                                                      \
    asm("outb %0, %1" : : "a"((uint8_t)value), "Nd"(port));

void setupPic() {
    // sadly I have to do this here, because the PIC will trigger before the
    // PIC driver has a chance to set it up
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 32);
    outb(0xA1, 40);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x1);
    outb(0xA1, 0x1);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
