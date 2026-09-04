#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    printf("[test-dma] Starting DMA & pagemap test...\r\n");
    fflush(stdout);

    // 1. Allocate 2 contiguous pages via MAP_ANON | MAP_SHARED
    size_t size = 8192;
    uint8_t *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (ptr == MAP_FAILED || ptr == NULL) {
        printf("[test-dma] ERROR: mmap failed!\r\n");
        fflush(stdout);
        return 1;
    }
    printf("[test-dma] Allocated 8KB shared anonymous memory at %p\r\n", (void *)ptr);
    fflush(stdout);

    // 2. Write pattern
    ptr[0] = 0xAA;
    ptr[4096] = 0xBB;

    // 3. Open /proc/self/pagemap
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        printf("[test-dma] ERROR: failed to open /proc/self/pagemap (fd=%d)\r\n", fd);
        fflush(stdout);
        return 1;
    }
    printf("[test-dma] Successfully opened /proc/self/pagemap (fd=%d)\r\n", fd);
    fflush(stdout);

    // 4. Query physical address for both pages
    uint64_t entry0 = 0, entry1 = 0;
    off_t offset0 = ((uintptr_t)ptr / 4096) * sizeof(uint64_t);
    off_t offset1 = (((uintptr_t)ptr + 4096) / 4096) * sizeof(uint64_t);

    ssize_t r0 = pread(fd, &entry0, sizeof(entry0), offset0);
    ssize_t r1 = pread(fd, &entry1, sizeof(entry1), offset1);

    if (r0 != sizeof(entry0) || r1 != sizeof(entry1)) {
        printf("[test-dma] ERROR: pread failed (r0=%d, r1=%d)\r\n", (int)r0, (int)r1);
        fflush(stdout);
        close(fd);
        return 1;
    }

    int present0 = (entry0 >> 63) & 1;
    int present1 = (entry1 >> 63) & 1;
    uint32_t pfn0 = (uint32_t)(entry0 & 0x7FFFFFFFFFFFFFULL);
    uint32_t pfn1 = (uint32_t)(entry1 & 0x7FFFFFFFFFFFFFULL);
    uint32_t phys0 = pfn0 * 4096;
    uint32_t phys1 = pfn1 * 4096;

    printf("[test-dma] Page 0: present=%d, pfn=0x%x, phys=0x%08x\r\n", present0, pfn0, phys0);
    printf("[test-dma] Page 1: present=%d, pfn=0x%x, phys=0x%08x\r\n", present1, pfn1, phys1);
    fflush(stdout);

    if (!present0 || !present1) {
        printf("[test-dma] ERROR: pages not present in pagemap!\r\n");
        fflush(stdout);
        close(fd);
        return 1;
    }

    if (phys1 != phys0 + 4096) {
        printf("[test-dma] ERROR: pages are not physically contiguous! diff=%d\r\n", (int)(phys1 - phys0));
        fflush(stdout);
        close(fd);
        return 1;
    }
    printf("[test-dma] SUCCESS: pages are verified physically contiguous!\r\n");
    fflush(stdout);

    close(fd);

    printf("[test-dma] Testing fork and shared memory write...\r\n");
    fflush(stdout);

    // 5. Test fork() shared memory (non-CoW) behavior
    pid_t pid = fork();
    if (pid < 0) {
        printf("[test-dma] ERROR: fork failed!\r\n");
        fflush(stdout);
        return 1;
    }

    if (pid == 0) {
        // In child: write new value to shared page
        printf("[test-dma-child] Writing to shared page...\r\n");
        fflush(stdout);
        ptr[0] = 0x42;
        printf("[test-dma-child] Write complete, exiting child...\r\n");
        fflush(stdout);
        exit(0);
    } else {
        int status = 0;
        printf("[test-dma-parent] Waiting for child...\r\n");
        fflush(stdout);
        waitpid(pid, &status, 0);
        printf("[test-dma-parent] Child finished with status %d\r\n", status);
        fflush(stdout);
        if (ptr[0] == 0x42) {
            printf("[test-dma] SUCCESS: child write observed in parent (shared non-CoW verified)!\r\n");
        } else {
            printf("[test-dma] ERROR: child write not observed (value=0x%02x, expected 0x42)!\r\n", ptr[0]);
            fflush(stdout);
            return 1;
        }
        fflush(stdout);
    }

    printf("[test-dma] All DMA tests PASSED!\r\n");
    fflush(stdout);
    return 0;
}
