#include "interrupts.h"

#include "process.h"

#include <interrupts.h>
#include <memory.h>
#include <service.h>
#include <stddef.h>
#include <syscall.h>
#include <util.h>

#define IDT_ENTRY(i)                                                           \
    idtEntries[i].offsetLow = U32(&idtHandler##i) & 0xFFFF;                    \
    idtEntries[i].offsetHigh = U32(&idtHandler##i) >> 16;

extern void *idt;
extern GDTEntry newGDT;
extern TSS tss;
extern ListElement *threads_to_process;
extern uint32_t getServiceId(Service *service);

ListElement *interruptSubscriptions[255];

__attribute__((section(".sharedFunctions")))
__attribute__((aligned(0x10))) IdtEntry idtEntries[256] = {};

ListElement *exceptionSubscriptions;

void onInterrupt(void *cr3, uint32_t d, uint32_t c, uint32_t b, uint32_t a,
                 uint32_t intNo) {
    foreach (interruptSubscriptions[intNo], ServiceFunction *, provider,
             { scheduleFunction(provider, NULL, intNo); })
        ;
    // TODO: here, a 'syscall(0)' should happen to allow other processes to also
    // do stuff
}

extern ProcessThread *current_thread;

void onException(void *ebp, void *cr2, void *cr3, uint32_t d, uint32_t c,
                 uint32_t b, uint32_t a, uint32_t intNo, uint32_t errorCode,
                 uint32_t eip) {
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
            // only one reference, this is ours now!
            VirtualAddress *address = (void *)&virtual;
            PageDirectoryEntry *directory =
                current_thread->process->memory_information.pageDirectory;
            void *pageTablePhysical =
                ADDRESS(directory[address->pageDirectoryIndex].pageTableID);
            PageTableEntry *pageTable = mapTemporaryA(pageTablePhysical);
            PageTableEntry *entry = &pageTable[address->pageTableIndex];
            mapping->copy_on_write = false;
            entry->writable = 1;
        } else {
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
        current_thread->resume = true;
        current_thread->function = 0;
        return;
    }

    while (1)
        ;
    // foreach (interruptSubscriptions[0], ServiceFunction *, provider, {
    //     Service *service = (Service *)current_thread->service;
    //     scheduleFunction(
    //         provider, current_thread->respondingTo, intNo, errorCode, eip,
    //         U32(getPhysicalAddress(
    //             ((Service
    //             *)current_thread->service)->pagingInfo.pageDirectory, ebp)),
    //         service->nameHash, getServiceId(service));
    // })
    //     ;
    // free(current_thread);
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
    outb(0xA1, 32);
    outb(0x21, 40);
    outb(0xA1, 0x02);
    outb(0x21, 0x04);
    outb(0x21, 0x1);
    outb(0xA1, 0x1);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void handleSubscribeInterruptSyscall(Thread *call) {
    ServiceFunction *provider = malloc(sizeof(ServiceFunction));
    Service *service = call->service;
    char *providerName = "INTERRUPT";
    provider->name = providerName;
    provider->address = PTR(call->parameters[1]);
    provider->service = call->service;
    listAdd(&interruptSubscriptions[call->parameters[0]], provider);
}
