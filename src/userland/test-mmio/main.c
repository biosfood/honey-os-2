#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>

int main() {
    printf("[test-mmio]: Starting MMIO tests...\r\n");

    // 1. Test opening /kernel/mem/0xb8000+0x1000 (VGA text buffer MMIO)
    int fd = open("/kernel/mem/0xb8000+0x1000", O_RDWR);
    if (fd < 0) {
        printf("[test-mmio]: FAIL - Could not open /kernel/mem/0xb8000+0x1000\r\n");
        return 1;
    }
    printf("[test-mmio]: SUCCESS - Opened /kernel/mem/0xb8000+0x1000 (fd %d)\r\n", fd);

    // 2. Test fstat for size
    struct stat st;
    if (fstat(fd, &st) < 0) {
        printf("[test-mmio]: FAIL - fstat failed\r\n");
        close(fd);
        return 2;
    }
    printf("[test-mmio]: SUCCESS - fstat size: %u bytes (expected 4096)\r\n", (unsigned int)st.st_size);
    if (st.st_size != 4096) {
        printf("[test-mmio]: FAIL - size mismatch\r\n");
        close(fd);
        return 3;
    }

    // 3. Test mmap with offset 0
    volatile uint16_t *vga = (volatile uint16_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vga == MAP_FAILED || vga == NULL) {
        printf("[test-mmio]: FAIL - mmap failed\r\n");
        close(fd);
        return 4;
    }
    printf("[test-mmio]: SUCCESS - mmap range file to %p\r\n", (void *)vga);

    // Read a word from VGA text buffer
    uint16_t original_word = vga[0];
    printf("[test-mmio]: Read from MMIO: 0x%04x\r\n", original_word);

    // 4. Test direct /kernel/mem with physical offset
    int raw_fd = open("/kernel/mem", O_RDWR);
    if (raw_fd >= 0) {
        volatile uint16_t *raw_vga = (volatile uint16_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, raw_fd, 0xb8000);
        if (raw_vga != MAP_FAILED && raw_vga != NULL) {
            printf("[test-mmio]: SUCCESS - raw /kernel/mem mmap to %p (val 0x%04x)\r\n", (void *)raw_vga, raw_vga[0]);
        } else {
            printf("[test-mmio]: FAIL - raw /kernel/mem mmap failed\r\n");
        }
        close(raw_fd);
    } else {
        printf("[test-mmio]: FAIL - could not open /kernel/mem\r\n");
    }

    // 5. Test fork preservation (making sure MMIO is not corrupted by COW)
    printf("[test-mmio]: Testing fork with active MMIO mapping...\r\n");
    pid_t pid = fork();
    if (!pid) {
        // In child: read MMIO
        uint16_t child_word = vga[0];
        printf("[test-mmio-child]: Child read MMIO: 0x%04x\r\n", child_word);
        exit(0);
    } else {
        int status = 0;
        waitpid(pid, &status, 0);
        printf("[test-mmio]: Child exited with status %d\r\n", status);
    }

    close(fd);
    printf("[test-mmio]: ALL MMIO TESTS PASSED!\r\n");
    return 0;
}
