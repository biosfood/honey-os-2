# HoneyOS Virtual File System (VFS) & Filesystems

This document explains the architecture of the HoneyOS Virtual File System, the mount routing mechanism, and each filesystem implemented in the kernel.

---

## 1. VFS Core Abstractions

All filesystem interfaces are defined in [`src/kernel/include/vfs.h`](file:///home/lukas/projects/honey-os-2/src/kernel/include/vfs.h).

```
 +------------------+
 |  FileDescriptor  | (per-process handle, returned by open)
 +--------+---------+
          |
          v
      +---+---+
      | File  |       (in-kernel inode equivalent, unique per underlying file)
      +---+---+
          |
          v
    +-----+------+
    | FileSystem |    (mounted filesystem instance)
    +-----+------+
          |
          v
  +-------+--------+
  | FileSystemType |  (driver vtable: getFile, read, write, create, getattr)
  +----------------+
```

### Key Data Structures

1. **`FileSystemType`**: The filesystem driver vtable:
   - `getFile(file_system, path, thread, result, scratchpad, options)`: Resolves a relative or absolute path within this filesystem to a `File*`. Can return `FILE_OPERATION_DONE` or `FILE_OPERATION_WILL_SCHEDULE` (for async/cooperative resolution).
   - `create(dir_file, path, type)`: Creates a new child file of type `FileType` in directory `dir_file`.
   - `read(file, data, size, offset, thread, descriptor, bytes_read)`: Reads from the file.
   - `write(file, data, size, offset, thread, descriptor, bytes_written)`: Writes to the file.
   - `getattr(file, stbuf, thread)`: Populates a POSIX `struct stat`.

2. **`FileSystem`**: Represents an active filesystem instance:
   - `name`: Human-readable identifier (e.g., `"RAMFS"`, `"PROCFS"`, `"PORT"`).
   - `type`: Pointer to the `FileSystemType` vtable.
   - `mountedInstances`: List of `Mount` records where this filesystem is attached.
   - `data`: Filesystem-specific private state (e.g. pointer to root directory).

3. **`File`**: Represents an individual file or directory (equivalent to a Linux inode):
   - `file_system`: Pointer back to the parent `FileSystem`.
   - `type`: `FILE_TYPE_DIRECTORY`, `FILE_TYPE_SYMLINK`, `FILE_TYPE_FILE`, `FILE_TYPE_FIFO`, `FILE_TYPE_SOCKET`, `FILE_TYPE_LINK`.
   - `data`: Private per-file data (e.g., buffer pointer for ramfs, port number for port fs).
   - `file_descriptors`: Linked list of open `FileDescriptor` handles referencing this file.

4. **`FileDescriptor`**: A per-process open handle (returned by `open`):
   - `id`: Numeric descriptor ID (0 = stdin, 1 = stdout, 2 = stderr, 3...).
   - `file`: Pointer to the underlying `File`.
   - `process`: Owning `Process`.
   - `offset`: Current read/write seek offset.
   - `fifo_data`: Embedded FIFO buffer and blocked reader thread reference if reading from a FIFO.
   - `read`, `write`, `execute`: Access permissions.

---

## 2. Mount Routing: `mountListFs`

HoneyOS uses a unified mount router filesystem called **`mountListFs`** ([`src/kernel/vfs/impl/mountListFs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/mountListFs.c)).

### How Mounts Work
When the kernel boots in [`loadAndScheduleSystemServices`](file:///home/lukas/projects/honey-os-2/src/kernel/main.c#L71-L90), it initializes the root mount list:
```c
FileSystem *fs = create_mount_list_file_system();
mount(fs, ramfs,                  "/",              "/");
mount(fs, kernelFs,               "/kernel/",       "/");
mount(fs, &cpuid_file_system,     "/dev/cpuid/",    "/");
mount(fs, &port_file_system,      "/dev/port/",     "/");
mount(fs, &interrupt_file_system, "/dev/interrupt/", "/");
```
And inside each container (`newContainer`):
```c
mount(fs, proc_fs,                "/proc/",         "/");
```

### Path Lookup & Prefix Matching
When `mountlist_get_file()` is called:
1. **Path Sanitization (`clean_path`)**: Normalizes slashes, resolves `.` (current directory) and `..` (parent directory).
2. **Longest Prefix Match (`mountlist_lookup`)**:
   - Searches all mounted entries for the mount point that matches the longest initial prefix of `path`.
   - Replaces the matched mount prefix with the target `pathOffset` and forwards the lookup to that sub-filesystem's `getFile`.
3. **Symlink Resolution**:
   - If the resolved file is `FILE_TYPE_SYMLINK` and not opened with `O_SYMLINK`:
   - Reads the symlink target via `read()`.
   - If relative, joins with the directory path; if absolute, restarts from the root.
   - Re-enters the lookup loop cleanly.
   - If `O_SYMLINK` is passed, lookup stops at the symlink and returns a descriptor to the symlink itself, allowing its target to be read with `read()` (see [**`docs/posix-deviations.md`**](file:///home/lukas/projects/honey-os-2/docs/posix-deviations.md)).

---

## 3. Filesystem Implementations

### 1. `ramfs` (Root Memory Filesystem)
- **Source**: [`src/kernel/vfs/impl/ramfs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/ramfs.c), [`ramfs.h`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/ramfs.h)
- **Default Mount**: Mounted at `/`.
- **Purpose**: General-purpose in-memory filesystem storing directories, binaries, configuration, and data.
- **Directory Structure**:
  - Each file/directory is a `RamFsFile`.
  - Directories store a linked list of child `RamFsFile` pointers in their `data` field.
  - Directories can be read via POSIX `getdents`/`readdir`: `ramfs_read()` uses `fill_dirent()` to generate standard `struct posix_dent` structures.
- **Creation**: `ramFsCreate()` handles `FILE_TYPE_FILE`, `FILE_TYPE_DIRECTORY`, and `FILE_TYPE_FIFO`.

### 2. `kernelfs` (Core Kernel Files)
- **Source**: [`src/kernel/vfs/impl/kernelfs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/kernelfs.c), [`kernelfs.h`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/kernelfs.h)
- **Default Mount**: Mounted at `/kernel/`.
- **Exposed Files**:
  - `/kernel/null`: Equivalent to Linux `/dev/null`. Writes discard data and return 0; reads return 0 bytes (EOF). Used for initial stdin/stdout/stderr handles of the init process.
  - `/kernel/pipe`: Dynamic anonymous pipe factory. Every `open("/kernel/pipe", ...)` instantiates a fresh dynamic `File` of type `FILE_TYPE_FIFO`. Reclaimed automatically via `kernelfs_close` when the last open descriptor referencing it is closed.
  - `/kernel/mem`: Placeholder for direct memory access.

### 3. `procfs` (Process & Thread Virtual Filesystem)
- **Source**: [`src/kernel/vfs/impl/procfs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/procfs.c)
- **Default Mount**: Mounted at `/proc/` per container.
- **Exposed Hierarchy**:
  - `/proc/self`: Symbolic link returning the calling process's PID as ASCII string. Can also be accessed as a directory prefix (e.g., `/proc/self/fd/3`).
  - `/proc/<pid>/exe`: Symbolic link returning the binary path of the executable.
  - `/proc/<pid>/fd/<id>` & `/proc/self/fd/<id>`: File descriptor duplication handle. Resolves numeric descriptor `<id>` from `process->openFileHandles` and returns the underlying `File*`. Opening this path with `open()` creates a new linked descriptor (with the requested `O_RDONLY`/`O_WRONLY`/`O_RDWR` modes), enabling POSIX `dup()` / `dup2()` semantics and pipe handle creation without new syscalls.
  - `/proc/<pid>/signal`: FIFO used to send signals to the process.
  - `/proc/<pid>/status`: Process exit & status FIFO:
    - **Write 4 bytes**: Calling process exits! `procfs_write` calls `process_exit(process, *(int32_t *)data)`.
    - **Read 4 bytes**: If the process has already exited, immediately returns the 4-byte exit code (`reap_info.exit_code`). If still running, the reading thread blocks on the FIFO until the child process exits. This is the core engine behind `waitpid()`/`wait4()`.
  - `/proc/<pid>/threads/self`: Symbolic link returning the calling thread's TID as ASCII string.
  - `/proc/<pid>/threads/<tid>/status`: Thread join & status FIFO:
    - **Write 4 bytes**: Thread exits! Calls `thread_exit(thread, result)`.
    - **Read 4 bytes**: Joins the thread by reading its exit pointer.

### 4. `port_file_system` (Direct x86 I/O Ports)
- **Source**: [`src/kernel/vfs/impl/ports.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/ports.c)
- **Default Mount**: Mounted at `/dev/port/`.
- **Mechanism**:
  - Access path: `/dev/port/<port_number>` (accepts decimal or hex `0x...`, e.g., `/dev/port/0x60`, `/dev/port/0x3F8`).
  - **Read**:
    - 1 byte: executes `in %dx, %al`
    - 2 bytes: executes `in %dx, %ax`
    - 4 bytes: executes `in %dx, %eax`
  - **Write**:
    - 1 byte: executes `out %al, %dx`
    - 2 bytes: executes `out %ax, %dx`
    - 4 bytes: executes `out %eax, %dx`
  - **Significance**: Allows userland device drivers (like serial ports, PIC, PIT) to perform raw hardware I/O with standard POSIX `open()`, `read()`, and `write()`.

### 5. `cpuid_file_system` (CPU Identification)
- **Source**: [`src/kernel/vfs/impl/cpuid.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/cpuid.c)
- **Default Mount**: Mounted at `/dev/cpuid/`.
- **Mechanism**:
  - Access path: `/dev/cpuid/<leaf_id>` (decimal or hex `0x...`).
  - **Read**: Executes `cpuid` with `eax = leaf_id`, returning up to 16 bytes: the resulting `ebx`, `edx`, `ecx`, `eax` registers.

### 6. `interrupt_file_system` (Hardware Interrupts as Files)
- **Source**: [`src/kernel/vfs/impl/interrupt.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/interrupt.c), [`src/kernel/interrupts/interrupts.c`](file:///home/lukas/projects/honey-os-2/src/kernel/interrupts/interrupts.c)
- **Default Mount**: Mounted at `/dev/interrupt/`.
- **Mechanism**:
  - 256 interrupt files: `/dev/interrupt/<0..255>`.
  - Each entry is backed by a `FILE_TYPE_FIFO`.
  - When a hardware interrupt fires, the CPU invokes `onInterrupt()` in `interrupts.c`.
  - `onInterrupt()` writes a `'1'` byte to `interrupt_files[intNo]`.
  - Any userland driver thread that called `read(fd, &buf, 1)` on `/dev/interrupt/<irq>` is immediately woken up and rescheduled.

### 7. `fifo` (Inter-Process Communication & Anonymous Pipes)
- **Source**: [`src/kernel/vfs/impl/fifo.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/impl/fifo.c)
- **Mechanism**:
  - Shared FIFO engine across `/kernel/pipe` anonymous pipes, `ramfs` named pipes, `procfs` status files, and `/dev/interrupt/`.
  - **Per-Descriptor Queue Architecture (Fan-Out / Pub-Sub)**:
    - Each open reader `FileDescriptor` maintains its own `FiFoData` queue and blocked thread pointer in `descriptor->fifo_data`.
    - Writers fan out their data across **all active reader descriptors** on that `File`. Each reader receives its own private copy of written data.
  - **Zero Readers ($0$ Readers)**:
    - If a writer writes to a FIFO that has no open reader descriptors, data is dropped (equivalent to writing to `/dev/null`). Data is never buffered in kernel space waiting for a prospective reader.
    - Readers that attach after writes have occurred only receive subsequent writes from the point of attachment.
  - **Blocking Read**: If reading from a FIFO whose per-descriptor queue is empty, the calling thread is paused (`fifo->thread = thread; thread->run = false`) and removed from the active runqueue `threads_to_process`.
  - **Writing / Wake-Up**: When data is written, it is transferred directly into the waiting reader's buffer (or queued in `fifo->queue` if not currently blocked), and the reader thread is placed back into `threads_to_process`.
  - **EOF on Writer Closure**: When all writer descriptors to a pipe close, `kernelfs_close` scans the pipe's reader descriptors and wakes any blocked threads with `*bytes_read = 0` (delivering EOF).

---

## 4. Bootstrapping Initrd into VFS

At boot time, GRUB loads `rootfs/initrd.tar` as a Multiboot module.
[`src/kernel/multiboot/tar.c`](file:///home/lukas/projects/honey-os-2/src/kernel/multiboot/tar.c):
1. Traverses the USTAR / POSIX tar archive in memory.
2. For each directory (`header->fileType == '5'`), calls `ramfs->type->create(parent, name, FILE_TYPE_DIRECTORY)`.
3. For each regular file (`header->fileType == '0'`), parses the octal size, creates the file in `ramfs`, and copies the contents via `ramfs->type->write()`.
4. Result: All programs compiled into `initrd/bin/` are directly available at `/bin/...` in `ramfs`.
