#include "pci.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../../build/musl/include/fcntl.h"
#include "../list.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>

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

void pciConfigWriteDword(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t offset, uint32_t data) {
    uint32_t address =
        (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000;
    write(config_address_fd, &address, 4);
    write(config_data_fd, &data, 4);
}

void checkBus(uint8_t);

#define CREATE_FILE(filename, ...)                                             \
    {                                                                          \
        char *path;                                                            \
        asprintf(&path, "/dev/pci/%i:%i.%i/" filename, bus, device,            \
                 function);                                                    \
        int fd = open(path, O_CREAT|O_RDWR);                                   \
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
    char *path;

    // make directories for the pci device: one for everything, and one for
    // BARs.
    asprintf(&path, "/dev/pci/%i:%i.%i", bus, device, function);
    mkdir(path, 0);
    free(path);

    asprintf(&path, "/dev/pci/%i:%i.%i/bar", bus, device, function);
    mkdir(path, 0);
    free(path);

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t bar_offset = 0x10 + 4 * i;
        uint32_t original = READ(bar_offset);
        pciDevice.bar[i] = original;

        if (original == 0 || original == 0xFFFFFFFF) {
            continue;
        }

        bool is_io = (original & 1) != 0;
        if (is_io) {
            // Skip PCI IO port BARs for now
            continue;
        }

        uint8_t mem_type = (original >> 1) & 3;
        bool is_64bit = (mem_type == 2);

        // Size the MMIO BAR
        pciConfigWriteDword(bus, device, function, bar_offset, 0xFFFFFFFF);
        uint32_t mask = READ(bar_offset);
        pciConfigWriteDword(bus, device, function, bar_offset, original);

        if (mask != 0 && mask != 0xFFFFFFFF) {
            uint32_t size = ~(mask & 0xFFFFFFF0) + 1;
            uint32_t phys_base = original & 0xFFFFFFF0;

            if (size > 0 && phys_base > 0) {
                char target[64];
                char linkpath[64];
                snprintf(target, sizeof(target), "/kernel/mem/0x%x+0x%x", phys_base, size);
                snprintf(linkpath, sizeof(linkpath), "/dev/pci/%i:%i.%i/bar/%i", bus, device, function, i);
                symlink(target, linkpath);
            }
        }

        if (is_64bit && i < 5) {
            i++;
            pciDevice.bar[i] = READ(0x10 + 4 * i);
        }
    }

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
    initialized = true;
}

void main() {
    config_address_fd = open("/dev/port/3320", O_RDWR);
    config_data_fd = open("/dev/port/3324", O_RDWR);
    mkdir("/dev/pci", 0);
    initializePci();

    int pcidevs = open("/dev/pci", O_SEARCH | O_RDONLY);
    struct stat stat;
    fstat(pcidevs, &stat);
    struct posix_dent *data = malloc(stat.st_size);
    int len = read(pcidevs, data, stat.st_size);
    struct posix_dent *current = data;
    char *filename = NULL;
    while (len) {
        if (!current->d_reclen) {
            break;
        }
        asprintf(&filename, "/dev/pci/%s/class_name", current->d_name);
        int fd = open(filename, O_RDONLY);
        free(filename);
        fstat(fd, &stat);
        char *classname = malloc(stat.st_size);
        read(fd, classname, stat.st_size);
        close(fd);

        asprintf(&filename, "/dev/pci/%s/class", current->d_name);
        fd = open(filename, O_RDONLY);
        free(filename);
        fstat(fd, &stat);
        char *class = malloc(stat.st_size);
        read(fd, class, stat.st_size);
        close(fd);

        printf("%s: class %s (%s)\n", current->d_name, class, classname);
        free(classname);
        free(class);

        char *bar_dir_name = NULL;
        asprintf(&bar_dir_name, "/dev/pci/%s/bar", current->d_name);
        int bar_dir_fd = open(bar_dir_name, O_SEARCH | O_RDONLY);
        free(bar_dir_name);
        if (bar_dir_fd >= 0) {
            struct stat bar_stat;
            fstat(bar_dir_fd, &bar_stat);
            if (bar_stat.st_size > 0) {
                struct posix_dent *bar_data = malloc(bar_stat.st_size);
                int bar_len = read(bar_dir_fd, bar_data, bar_stat.st_size);
                struct posix_dent *bar_curr = bar_data;
                while (bar_len > 0) {
                    if (!bar_curr->d_reclen) {
                        break;
                    }
                    char *bar_link = NULL;
                    asprintf(&bar_link, "/dev/pci/%s/bar/%s", current->d_name, bar_curr->d_name);
                    int link_fd = open(bar_link, O_RDONLY | O_SYMLINK);
                    if (link_fd >= 0) {
                        char target_buf[128];
                        memset(target_buf, 0, sizeof(target_buf));
                        int n = read(link_fd, target_buf, sizeof(target_buf) - 1);
                        close(link_fd);
                        if (n > 0) {
                            printf("  bar %s -> %s\n", bar_curr->d_name, target_buf);
                        }
                    }
                    free(bar_link);
                    bar_len -= bar_curr->d_reclen;
                    bar_curr = ((void *)bar_curr) + bar_curr->d_reclen;
                }
                free(bar_data);
            }
            close(bar_dir_fd);
        }

        len -= current->d_reclen;
        current = ((void *)current) + current->d_reclen;
    }
    free(data);
    close(pcidevs);
}
