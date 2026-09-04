# Deviations from POSIX in HoneyOS & HoneyOS-Musl

This document details where HoneyOS and `honey-os-musl` intentionally deviate from standard POSIX, why these design decisions were made, and how to use these interfaces.

---

## 1. The `O_SYMLINK` Flag (`0x1000`)

The primary direct addition to standard POSIX header constants in `honey-os-musl` is **`O_SYMLINK`** in [`include/fcntl.h`](file:///home/lukas/projects/musl/include/fcntl.h#L47):

```c
#define O_SYMLINK 0x1000
```

### Purpose & Rationale
In standard POSIX:
- `open()` automatically follows symbolic links unless `O_NOFOLLOW` is specified.
- However, passing `O_NOFOLLOW` to a symlink causes `open()` to **fail** with `errno = ELOOP`.
- In Linux, `O_PATH | O_NOFOLLOW` allows opening a descriptor to the symlink, but `read(fd, ...)` on such a descriptor returns `EBADF`. Reading the link target strictly requires the dedicated system call **`readlink()`** or **`readlinkat()`**.

In HoneyOS:
- To adhere to the microkernel philosophy of minimizing system calls, **no dedicated `readlink` system call is implemented in the kernel**.
- Instead, passing `O_SYMLINK` instructs the VFS mount router ([`src/kernel/vfs/impl/mountListFs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/mountListFs.c#L177-L181)) to stop path resolution at the symlink itself and return a standard `FileDescriptor` pointing to the symlink file.
- The symlink target can then be read directly using standard **`read(fd, buf, size)`**!

### Example Usage: Reading a Symlink Target Directly

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    char target[256];
    
    // Open the symlink itself without dereferencing
    int fd = open("/proc/self/exe", O_RDONLY | O_SYMLINK);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Read the symlink destination directly
    ssize_t bytes_read = read(fd, target, sizeof(target) - 1);
    close(fd);
    
    if (bytes_read > 0) {
        target[bytes_read] = '\0';
        printf("Binary path: %s\n", target);
    }
    return 0;
}
```

### How `readlink()` Can Be Implemented in Userland
Because of `O_SYMLINK`, a standard POSIX `readlink()` wrapper can be implemented entirely in userland without a dedicated syscall:

```c
ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    int fd = open(path, O_RDONLY | O_SYMLINK);
    if (fd < 0) return -1;
    ssize_t ret = read(fd, buf, bufsiz);
    close(fd);
    return ret;
}
```

---

## 2. Other Architectural Deviations from Traditional POSIX

Beyond `O_SYMLINK`, HoneyOS replaces several traditional POSIX system calls with VFS operations:

### 1. Process Exit & Wait (`exit`, `waitpid`, `wait4`)
- **POSIX Model**: Dedicated `exit()` system call terminates the process; parent calls `waitpid()`/`wait4()` to receive a SIGCHLD notification and exit status.
- **HoneyOS Model**:
  - Exiting is done by opening `/proc/self/status` and writing the 4-byte exit status integer.
  - Waiting is done by opening `/proc/<pid>/status` and reading 4 bytes. If the target process is still running, the kernel FIFO blocks the calling thread until the process terminates.

### 2. Thread Exit & Join (`pthread_exit`, `pthread_join`)
- **POSIX / Linux Model**: Linux uses `sys_exit` on the thread TID and futex wait (`FUTEX_WAIT`) on the thread's `clear_child_tid` address.
- **HoneyOS Model**:
  - `pthread_exit()` writes the 4-byte return pointer to `/proc/self/threads/self/status`.
  - `pthread_join()` reads 4 bytes from `/proc/self/threads/<tid>/status`, blocking until the target thread exits and returns its exit value.

### 3. Heap Allocation (`brk`)
- **POSIX / Linux Model**: Applications expand the data segment by requesting higher break addresses with `brk()`.
- **HoneyOS Model**:
  - HoneyOS does not implement a growing heap break in the kernel.
  - `honey-os-musl` implements a `_brk_` stub that always returns a constant break address (`_end`).
  - Under Linux/POSIX semantics, refusing to grow the break signals "out of memory", which automatically redirects musl's `malloc()` to use `mmap(MAP_ANON)`.

### 4. File Creation (`O_CREAT`)
- **POSIX / Linux Model**: `open(path, O_CREAT | ...)` handles creation atomically in the kernel filesystem driver.
- **HoneyOS Model**:
  - Kernel syscall `HONEY_SYS_FILE_CREATE` takes a directory descriptor and child name.
  - `_open_()` in `honey-os-musl` ([`translate_call.c`](file:///home/lukas/projects/musl/src/internal/translate_call.c#L172-L182)) attempts `HONEY_SYS_OPEN` first; if the file doesn't exist and `O_CREAT` is set, it opens the parent directory using `O_SEARCH`, calls `_create_()`, closes the parent directory, and re-opens the created file.

### 5. Anonymous Pipes & Descriptor Duplication (`pipe()`, `dup()`, `dup2()`)

- **POSIX / Linux Model**:
  - Dedicated `pipe(int pipefd[2])` or `pipe2()` system call allocates two connected file descriptors backed by a single kernel ring-buffer queue.
  - Multiple readers compete for bytes in the same shared buffer: a byte read by one reader is consumed and unavailable to other readers.
  - Dedicated `dup(oldfd)` and `dup2(oldfd, newfd)` system calls duplicate file descriptors.
  - Writing to a pipe with no readers raises `SIGPIPE` or returns `EPIPE`.
  - Closing all write handles delivers EOF (`read()` returns 0) to blocked readers.

- **HoneyOS Model**:
  - **Zero New System Calls**: Neither `pipe()`, `dup()`, nor `dup2()` are implemented as kernel syscalls. All pipe operations and descriptor duplications are performed through the standard VFS (`open()`, `close()`, `read()`, `write()`).
  - **Dynamic Pipe Allocation via `/kernel/pipe`**:
    Opening `/kernel/pipe` (e.g., `int rfd = open("/kernel/pipe", O_RDONLY);`) instantiates a new dynamic `File` of type `FILE_TYPE_FIFO` in `kernelfs`.
  - **Descriptor Duplication via `/proc/self/fd/<id>`**:
    To create a paired write handle (or duplicate an existing descriptor), userland opens `/proc/self/fd/<id>` with the desired access flags:
    ```c
    char proc_path[32];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", rfd);
    int wfd = open(proc_path, O_WRONLY);
    ```
    This resolves the descriptor ID to the underlying `File*` and allocates a new `FileDescriptor` with the requested read/write permissions.
  - **Per-Reader Queue (Fan-Out / Broadcast) Architecture**:
    Unlike POSIX pipes where readers share a single queue, HoneyOS maintains a separate `FiFoData` queue for **each open reader descriptor**:
    - When a writer writes to a pipe, the data is fanned out to **every active reader descriptor** open on that pipe.
    - Multiple readers do not compete for bytes; each reader receives its own copy of the stream.
  - **No Readers ($0$ Readers) = `/dev/null`**:
    Because HoneyOS supports multiple reader queues, writing to a pipe with no open reader descriptors causes the data to be discarded (equivalent to writing to `/dev/null`). Data is **not** buffered in kernel memory waiting for a reader to appear later.
  - **Late Attachment**:
    If a reader descriptor is opened or duplicated after writes have already occurred, that reader only receives subsequent writes from that point forward.
    > [!IMPORTANT]
    > **Rule for Userspace**: Always establish and open your reading descriptor(s) **before** allowing the writer process/thread to produce data.
  - **Lifecycle & Automatic Reclamation**:
    Pipe lifetime is governed entirely by open descriptors via the filesystem driver's `close` callback (`FileSystemType.close`). When the last descriptor referencing the pipe is closed (`file->file_descriptors == NULL`), `kernelfs` automatically frees the in-memory `File`.
  - **EOF Semantics**:
    When all writer descriptors on a pipe are closed, `kernelfs` wakes any blocked reader threads on that pipe, returning `bytes_read = 0` (standard POSIX EOF).
    *Note on Supervision*: In HoneyOS supervisor designs, EOF simply indicates that the monitored process closed its standard output; it does not necessarily imply a crash or immediate process termination.

#### Idiomatic Userspace Pattern: Redirecting Child Output to a Pipe

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void spawn_piped_child() {
    char path[32];

    // 1. Create anonymous pipe read handle
    int rfd = open("/kernel/pipe", O_RDONLY);

    // 2. Obtain write handle via descriptor duplication
    snprintf(path, sizeof(path), "/proc/self/fd/%d", rfd);
    int wfd = open(path, O_WRONLY);

    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect stdout (fd 1) to the pipe
        close(STDOUT_FILENO);
        
        // open() always allocates the lowest free non-negative fd (now 1)
        snprintf(path, sizeof(path), "/proc/self/fd/%d", wfd);
        open(path, O_WRONLY);

        // Clean up temporary descriptors
        close(rfd);
        close(wfd);

        char *args[] = { "/bin/my-daemon", NULL };
        execv(args[0], args);
        exit(1);
    }

    // Parent: close write handle so only child holds writer status
    close(wfd);

    // Read output from child until EOF
    char buf[256];
    ssize_t n;
    while ((n = read(rfd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("[daemon]: %s", buf);
    }

    close(rfd);
}
```

---

### 6. Hardware I/O & Interrupts
- **Traditional Model**: Direct privileged x86 instructions (`in`/`out`), `iopl`/`ioperm` syscalls, and signal delivery for interrupts.
- **HoneyOS Model**:
  - Reading/writing x86 I/O ports via `/dev/port/<port_num>`.
  - Receiving hardware IRQ events via blocking reads on `/dev/interrupt/<irq_num>`.
