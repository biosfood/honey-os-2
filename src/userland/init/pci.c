#include "pci.h"

#include "unistd.h"
#include <sys/stat.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fnctl.h>

#include <hlib.h>

#define GET_HEADER                                                             \
    if (!initialized) {                                                        \
        initializePci();                                                       \
    }                                                                          \
    if (deviceId >= deviceCount) {                                             \
        return 0;                                                              \
    }                                                                          \
    PciDevice *device = listGet(pciDevices, deviceId);

#define READ(offset) (pciConfigRead(bus, device, function, (offset)))
#define READ16(offset) (READ(offset) & 0xFFFF)
#define READ8(offset) (READ(offset) & 0xFF)
#define VENDOR_ID() (pciConfigRead(bus, device, function, 0) & 0xFFFF)

extern int portFd;

#define ioOut(port, data, len)                                                 \
    {                                                                          \
        buf = data;                                                            \
        pwrite(portFd, &buf, len, port);                                       \
    }
#define ioIn(port, len)                                                        \
    ({                                                                         \
        pread(portFd, &buf, len, port);                                        \
        buf;                                                                   \
    })
#define U32(x) ((uint32_t)(uintptr_t)(x))

char *classNames[] = {
    "Unclassified",
    "Mass Storage Controller",
    "Network controller",
    "Display controller",
    "Multimedia controller",
    "Memory Controller",
    "Bridge",
    "Simple Communication controller",
    "Base System Peripheral",
    "Input Device controller",
    "Docking station",
    "Processor",
    "Serial bus controller",
    "Wireless controller",
    "intelligent controller",
    "sattelite communication controller",
    "encryption controller",
    "signal processing controller",
    "processing accelerator",
    "non-essential instrumentation",
};

bool checkedBuses[256];

uint32_t deviceCount = 0;

ListElement *pciDevices = NULL;
bool initialized = false;

uint32_t pciConfigRead(uint32_t bus, uint32_t device, uint32_t function,
                       uint8_t offset) {
    uint32_t buf;
    uint32_t address = ((bus << 16) | (device << 11) | (function << 8) |
                        (offset & 0xFC) | 0x80000000);
    ioOut(0xCF8, address, 4);
    uint32_t result = ioIn(0xCFC, 4) >> ((offset % 4) * 8);
    return result;
}

void pciConfigWriteByte(uint32_t bus, uint32_t device, uint32_t function,
                        uint8_t offset, uint32_t data) {
    uint32_t buf;
    uint32_t address =
        (bus << 16) | (device << 11) | (function << 8) | offset | 0x80000000;
    ioOut(0xCF8, address, 4);
    ioOut(0xCFC, data, 2);
}

void pciConfigWriteWord(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint16_t data) {
    pciConfigWriteByte(bus, device, function, offset, (uint8_t)data);
    pciConfigWriteByte(bus, device, function, offset + 1, (uint8_t)(data >> 8));
}

void checkBus(uint8_t);

void checkFunction(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t class = READ8(0xB);
    if (!class || class == 0xFF) {
        return;
    }
    PciDevice *pciDevice = malloc(sizeof(PciDevice));
    pciDevice->bus = bus;
    pciDevice->device = device;
    pciDevice->function = function;
    pciDevice->class = class;
    pciDevice->vendorId = READ16(0x00);
    pciDevice->deviceId = READ16(0x02);
    pciDevice->configuration = READ16(0x04);
    pciDevice->programmingInterface = READ8(0x09);
    pciDevice->subclass = READ8(0x0A);
    uint32_t temp;
    for (uint8_t i = 0; i < 6; i++) {
        pciDevice->bar[i] = (temp = READ(0x10 + 4 * i));
    }
    char *path;
    asprintf(&path, "/dev/pci/%i:%i.%i", bus, device, function);
    mkdir(path, 0);
    free(path);

    asprintf(&path, "/dev/pci/%i:%i.%i/class_name", bus, device, function);
    int fd = open(path,O_CREAT);
    write(fd, classNames[pciDevice->class], strlen(classNames[pciDevice->class]) + 1);
    close(fd);
    free(path);

    pciDevice->id = listCount(pciDevices);
    listAdd(&pciDevices, pciDevice);
    if (class == 6 && pciDevice->subclass == 4) {
        checkBus(READ8(0x19));
    }
}

void checkDevice(uint8_t bus, uint8_t device) {
    uint32_t function = 0;
    uint16_t vendorID = VENDOR_ID();
    if (vendorID == 0xFFFF) {
        return;
    }
    if (READ8(0x0E) & 0x80) {
        // multifunction device
        for (; function < 8; function++) {
            if (VENDOR_ID() != 0xFFFF) {
                checkFunction(bus, device, function);
            }
        }
    } else {
        checkFunction(bus, device, 0);
    }
}

void checkBus(uint8_t bus) {
    if (checkedBuses[bus]) {
        return;
    }
    checkedBuses[bus] = true;
    for (uint16_t device = 0; device < 32; device++) {
        checkDevice(bus, device);
    }
}

void initializePci() {
    if (!(pciConfigRead(0, 0, 0, 0x0E) & 0x80)) {
        checkBus(0);
    } else {
        for (uint8_t bus = 0; bus < 8; bus++) {
            checkBus(bus);
        }
    }
    deviceCount = listCount(pciDevices);
    initialized = true;
}

void initPCI() {
    printf("indexing PIC bus... ");
    mkdir("/dev/pci", 0);
    initializePci();
    printf("done.\n");
}
