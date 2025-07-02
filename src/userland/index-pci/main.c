#include "pci.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fnctl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int config_address_fd, config_data_fd;

uint32_t pciConfigRead(uint32_t bus, uint32_t device, uint32_t function,
                       uint8_t offset) {
    uint32_t address = ((bus << 16) | (device << 11) | (function << 8) |
                        (offset & 0xFC) | 0x80000000);
    write(config_address_fd, &address, 4);
    uint32_t result;
    read(config_data_fd, &result, 4);
    return result >> ((offset % 4) * 8);
}

void pciConfigWriteByte(uint32_t bus, uint32_t device, uint32_t function,
                        uint8_t offset, uint32_t data) {
    uint32_t address =
        (bus << 16) | (device << 11) | (function << 8) | offset | 0x80000000;
    write(config_address_fd, &address, 4);
    write(config_data_fd, &data, 1);
}

void pciConfigWriteWord(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint16_t data) {
    pciConfigWriteByte(bus, device, function, offset, (uint8_t)data);
    pciConfigWriteByte(bus, device, function, offset + 1, (uint8_t)(data >> 8));
}

void checkBus(uint8_t);

#define CREATE_FILE(filename, ...)                                             \
    {                                                                          \
        char *path;                                                            \
        asprintf(&path, "/dev/pci/%i:%i.%i/" filename, bus, device,           \
                 function);                                                    \
        int fd = open(path, O_CREAT);                                          \
        dprintf(fd, __VA_ARGS__);                                              \
        close(fd);                                                             \
        free(path);                                                            \
    }

void checkFunction(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t class = READ8(0xB);
    if (!class || class == 0xFF) {
        return;
    }
    PciDevice pciDevice;
    pciDevice.bus = bus;
    pciDevice.device = device;
    pciDevice.function = function;
    pciDevice.class = class;
    pciDevice.vendorId = READ16(0x00);
    pciDevice.deviceId = READ16(0x02);
    pciDevice.configuration = READ16(0x04);
    pciDevice.programmingInterface = READ8(0x09);
    pciDevice.subclass = READ8(0x0A);
    for (uint8_t i = 0; i < 6; i++) {
        pciDevice.bar[i] = READ(0x10 + 4 * i);
    }
    char *path;

    // make directories for the pci device: one for everything, and one for
    // BARs.
    asprintf(&path, "/dev/pci/%i:%i.%i", bus, device, function);
    mkdir(path, 0);
    free(path);

    asprintf(&path, "/dev/pci/%i:%i.%i/bar", bus, device, function);
    mkdir(path, 0);
    free(path);

    CREATE_FILE("class", "%i", pciDevice.class);
    CREATE_FILE("class_name", "%s", classNames[pciDevice.class]);
    CREATE_FILE("vendor", "%i", pciDevice.vendorId);
    CREATE_FILE("device", "%i", pciDevice.deviceId);
    CREATE_FILE("configuration", "%i", pciDevice.configuration);

    CREATE_FILE("programming_interface", "%i", pciDevice.programmingInterface);
    CREATE_FILE("subclass", "%i", pciDevice.subclass);

    if (class == 6 && pciDevice.subclass == 4) {
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

void main() {
    config_address_fd = open("/dev/port/3320", 0);
    config_data_fd = open("/dev/port/3324", 0);
    printf("indexing PCI bus... ");
    mkdir("/dev/pci", 0);
    initializePci();
    printf("done.\n");

    int pcidevs = open("/dev/pci", 0);
    struct stat stat;
    fstat(pcidevs, &stat);
    posix_dirent *data = malloc(stat.st_size);
    int len = read(pcidevs, data, stat.st_size);
    posix_dirent *current = data;
    char *filename = NULL;
    while (len) {
        if (!current->d_reclen) {
            break;
        }
        asprintf(&filename, "/dev/pci/%s/class_name", current->d_name);
        int fd = open(filename, 0);
        free(filename);
        fstat(fd, &stat);
        char *classname = malloc(stat.st_size);
        read(fd, classname, stat.st_size);
        close(fd);

        asprintf(&filename, "/dev/pci/%s/class", current->d_name);
        fd = open(filename, 0);
        free(filename);
        fstat(fd, &stat);
        char *class = malloc(stat.st_size);
        read(fd, class, stat.st_size);
        close(fd);

        printf("%s: class %s (%s)\n", current->d_name, class, classname);
        free(classname);
        free(class);
        len -= current->d_reclen;
        current = ((void *)current) + current->d_reclen;
    }
    free(data);
    close(pcidevs);
    printf("done\n");
    while (1) {
        read(0, &data, 0);
    }
}
