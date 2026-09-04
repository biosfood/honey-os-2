# HoneyOS System Calls Reference

This document details how system calls work in HoneyOS: the low-level hardware mechanism (`sysenter`/`sysexit`), calling conventions, the in-kernel dispatch table, detailed behavior of every implemented syscall, and how POSIX operations are handled.

---

## 1. Low-Level Syscall Mechanism (`sysenter` & `sysexit`)

Unlike traditional x86 kernels that use `int 0x80`, HoneyOS uses Intel Pentium II+ fast system call instructions: **`sysenter`** and **`sysexit`**.

### Kernel MSR Setup
During boot (`kernelMain()` -> `setupSyscalls()` in [`src/kernel/syscalls/syscall.c`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscall.c#L14-L18)), the kernel sets up Model-Specific Registers:
- **MSR 0x174 (`IA32_SYSENTER_CS`)**: Set to kernel code segment selector `0x08`.
- **MSR 0x175 (`IA32_SYSENTER_ESP`)**: Allocated a 4KB kernel stack (`malloc(0x1000) + 0x1000`).
- **MSR 0x176 (`IA32_SYSENTER_EIP`)**: Points to [`syscallStub`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscallStub.asm#L11-L28).

### Invocation Flow: Userland to Kernel
When userland issues `syscall_impl(...)` in `honey-os-musl` ([`src/internal/i386/syscall_impl.s`](file:///home/lukas/projects/musl/src/internal/i386/syscall_impl.s)):
1. Saves all general-purpose registers (`pusha`) and SSE vector registers (`xmm0`-`xmm7`).
2. Places arguments into registers:
   - `eax` = Function number (Syscall ID)
   - `ebx` = Parameter 0
   - `ecx` = Parameter 1
   - `edx` = Parameter 2
   - `esi` = Parameter 3
3. Pushes return address `$end` to the user stack, saves `%esp` into `%edi`.
4. Executes `sysenter`.

### Kernel Entry: `syscallStub`
In [`src/kernel/syscalls/syscallStub.asm`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscallStub.asm#L11-L28):
1. Immediately switches page directory to kernel CR3: `mov cr3, 0x500000`.
2. Pushes parameters in C-calling convention: `esi`, `edx`, `ecx`, `ebx`, `eax`, `edi` (user stack pointer).
3. Calls [`handleSyscall(void *esp, uint32_t function, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3)`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscall.c#L22-L38).
4. `handleSyscall()` records the function and parameters into `current_thread` and returns.
5. `syscallStub` restores the kernel scheduler stack via `temporaryESP` and returns to `processThread()`.

### Returning to Userland: `sysexit`
When the kernel scheduler runs a thread ([`runFunction`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscallStub.asm#L41-L59)):
1. Reloads user TLS base register `%gs` using `set_thread_area_32(thread->thread_pointer_gs)`.
2. Loads `thread_esp` into `ecx` and return instruction pointer into `edx`.
3. Loads `thread_cr3` into `cr3` (switches back to process page directory).
4. Loads `thread_return_value` into `eax`.
5. Executes `sysexit` to transition back to user mode (Ring 3).

---

## 2. Kernel Syscall Dispatch Table

The dispatch array is defined in [`src/kernel/syscalls/syscall.c`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscall.c#L51-L88):

```c
void (*syscallHandlers[])(ProcessThread *) = {
    [23] = (void *) &handleForkSyscall,
    [25] = (void *) &handlePthreadCreateSyscall,
    [26] = (void *) &handleOpenSyscall,
    [27] = (void *) &handleReadSyscall,
    [28] = (void *) &handleWriteSyscall,
    [29] = (void *) &handleCreateFileSyscall,
    [30] = (void *) &handleMmapSyscall,
    [31] = (void *) &handleMunmapSyscall,
    [32] = (void *) &handleCloseSyscall,
    [33] = (void *) &handleStatSyscall,
    [34] = (void *) &handleExecSyscall,
    [35] = (void *) &handleSetThreadPointerSyscall,
};
```

*(Note: Unused slots between 0 and 22, and index 24 are NULL).*

---

## 3. Syscall Reference Table

| Syscall ID | Musl Constant | Kernel Handler | Source Location | Summary |
|---|---|---|---|---|
| **0** | `HONEY_SYS_RUN` | *(none)* | [`src/kernel/syscalls/syscall.c`](file:///home/lukas/projects/honey-os-2/src/kernel/syscalls/syscall.c) | Thread resume placeholder |
| **23** | `HONEY_SYS_FORK` | `handleForkSyscall` | [`src/kernel/process/process.c`](file:///home/lukas/projects/honey-os-2/src/kernel/process/process.c#L25) | Clones process with Copy-On-Write |
| **25** | `HONEY_SYS_PTHREAD_CREATE` | `handlePthreadCreateSyscall` | [`src/kernel/process/threads.c`](file:///home/lukas/projects/honey-os-2/src/kernel/process/threads.c#L4) | Creates a new thread in the same address space |
| **26** | `HONEY_SYS_OPEN` | `handleOpenSyscall` | [`src/kernel/vfs/syscalls/open.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/syscalls/open.c#L22) | Opens a file/device by path via VFS |
| **27** | `HONEY_SYS_READ` | `handleReadSyscall` | [`src/kernel/vfs/syscalls/read.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/syscalls/read.c#L21) | Reads bytes from a file descriptor |
| **28** | `HONEY_SYS_WRITE` | `handleWriteSyscall` | [`src/kernel/vfs/syscalls/write.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/syscalls/write.c#L22) | Writes bytes to a file descriptor |
| **29** | `HONEY_SYS_FILE_CREATE` | `handleCreateFileSyscall` | [`src/kernel/vfs/syscalls/create_file.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/syscalls/create_file.c#L8) | Creates file, directory, or FIFO in directory |
| **30** | `HONEY_SYS_MMAP` | `handleMmapSyscall` | [`src/kernel/memory/memorySyscalls.c`](file:///home/lukas/projects/honey-os-2/src/kernel/memory/memorySyscalls.c#L18) | Maps anonymous pages into address space |
| **31** | `HONEY_SYS_MUNMAP` | `handleMunmapSyscall` | [`src/kernel/memory/memorySyscalls.c`](file:///home/lukas/projects/honey-os-2/src/kernel/memory/memorySyscalls.c#L65) | Unmaps virtual page |
| **32** | `HONEY_SYS_CLOSE` | `handleCloseSyscall` | [`src/kernel/vfs/syscalls/close.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/syscalls/close.c#L9) | Closes and deallocates a file descriptor |
| **33** | `HONEY_SYS_STAT` | `handleStatSyscall` | [`src/kernel/vfs/vfs.c`](file:///home/lukas/projects/honey-os-2/src/kernel/vfs/vfs.c#L80) | Retrieves `struct stat` for an open file |
| **34** | `HONEY_SYS_EXEC` | `handleExecSyscall` | [`src/kernel/process/process.c`](file:///home/lukas/projects/honey-os-2/src/kernel/process/process.c#L119) | Replaces address space with a new ELF binary |
| **35** | `HONEY_SYS_SET_GP` | `handleSetThreadPointerSyscall` | [`src/kernel/process/threads.c`](file:///home/lukas/projects/honey-os-2/src/kernel/process/threads.c#L21) | Sets thread-pointer (`%gs`) for TLS |

---

## 4. Syscall Details & Semantics

### 23: `handleForkSyscall`
- **Arguments**: None.
- **Return Value**: Child PID to parent process; `0` to child process.
- **Behavior**:
  - Allocates a new `Process` and page directory.
  - Duplicates all `VirtualMemoryEntry` structures and `MemoryMapping` descriptors.
  - Implements **Copy-On-Write (COW)**:
    - Sets `mapping->copy_on_write = true` and increments `physical->refcount`.
    - Updates page table entries in **both** parent and child to `writable = false`.
  - Duplicates all open `FileDescriptor` handles in `openFileHandles`.
  - Duplicates the current calling thread with identical `%esp` and `thread_pointer_gs`.
  - Enqueues both the parent and child threads to `threads_to_process`.

### 25: `handlePthreadCreateSyscall`
- **Arguments**:
  - `p0`: `void *stack` (initial user stack pointer for new thread).
  - `p1`: `uint32_t tls` (thread-local storage pointer).
- **Return Value**: Newly created thread ID (`new_thread->id`).
- **Behavior**:
  - Allocates a new `ProcessThread` sharing the same `Process` (same page directory / CR3 and open file handles).
  - Initializes thread files in procfs (`initialize_thread_files`), giving the thread its own `/proc/<pid>/threads/<tid>/status` FIFO.
  - Enqueues both caller and new thread to the scheduler.

### 26: `handleOpenSyscall`
- **Arguments**:
  - `p0`: `const char *path` (pointer in user address space).
  - `p1`: `int oflag` (POSIX open flags: `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_SEARCH`, `O_CREAT`).
- **Return Value**: File descriptor ID (`>= 0`) on success, `-1` on failure.
- **Behavior**:
  - Implemented as a 3-stage state machine (`OPEN_PRE`, `OPEN_MAIN`, `OPEN_POST`) stored in `thread->threadProcessingState`.
  - Copies string from user space with `copy_string_from_process`.
  - Invokes `vfs->type->getFile(...)` on the root mount list.
  - Verifies file type and permissions (e.g. directories require `O_SEARCH`).
  - Calls `allocateFileDescriptor(thread->process)` and links descriptor to file.

### 27: `handleReadSyscall`
- **Arguments**:
  - `p0`: `int fd`
  - `p1`: `void *buffer` (user destination buffer)
  - `p2`: `size_t nbyte` (bytes to read)
  - `p3`: `off_t offset` (used for `pread` or 0)
- **Return Value**: Bytes transferred on success, `-EBADF` if invalid descriptor.
- **Behavior**:
  - Verifies `fd->read` permission.
  - Allocates kernel buffer `state->data_buffer` of size `nbyte`.
  - Invokes `file->file_system->type->read(...)`.
  - If reading from a FIFO with no data ready, the thread blocks (is not added back to `threads_to_process`) until data is written.
  - In `READ_POST`, transfers bytes from kernel to userland via `copy_from_kernel_to_process`, frees the buffer, and resumes the thread.

### 28: `handleWriteSyscall`
- **Arguments**:
  - `p0`: `int fd`
  - `p1`: `const void *buffer` (user source buffer)
  - `p2`: `size_t nbyte` (bytes to write)
  - `p3`: `off_t offset`
- **Return Value**: Bytes written on success, `-EBADF` on invalid descriptor.
- **Behavior**:
  - Verifies `fd->write` permission.
  - Copies user buffer into kernel buffer via `copy_from_process_to_kernel`.
  - Invokes `file->file_system->type->write(...)`.
  - If writing to a FIFO, checks for threads blocked waiting on read, wakes them up, or buffers unconsumed data.
  - In `WRITE_POST`, frees kernel buffer and sets `returnValue = bytes_written`.

### 29: `handleCreateFileSyscall`
- **Arguments**:
  - `p0`: `int dir_fd` (directory file descriptor)
  - `p1`: `const char *name` (filename relative to `dir_fd`, must not contain `/`)
  - `p2`: `enum FileType type` (`FILE_TYPE_FILE`, `FILE_TYPE_FIFO`, `FILE_TYPE_DIRECTORY`, etc.)
- **Return Value**: `0` on success, `-1` on error.
- **Behavior**:
  - Finds the directory file descriptor and validates that `type == FILE_TYPE_DIRECTORY`.
  - Copies filename string from user space.
  - Calls `file_descriptor->file->file_system->type->create(...)`.

### 30: `handleMmapSyscall`
- **Arguments**:
  - `p0`: `MmapArgs *args` (pointer to packed structure in user space: `addr`, `len`, `prot`, `flags`, `filedes`, `off`).
- **Return Value**: Virtual address of mapped region, or `-1` on error.
- **Behavior**:
  - Copies `MmapArgs` from user space.
  - Supports `MAP_ANON` mappings.
  - If `addr` is NULL, searches for a contiguous unallocated virtual address range using `findMultiplePages`.
  - Reserves the virtual page range via `reservePagesCount`.
  - Allocates physical frames via `get_single_page_physical_memory_entry()`, zeroes them using `mapTemporaryA`, and maps each page into the process's page directory as writable.
  - Creates and tracks a `VirtualMemoryEntry` of type `MEM_TYPE_HEAP`.

### 31: `handleMunmapSyscall`
- **Arguments**:
  - `p0`: `void *addr`
  - `p1`: `size_t len`
- **Return Value**: `0`.
- **Behavior**:
  - Computes `virtualPageId = PAGE_ID(addr)` and unmaps via `giveUpPage`.

### 32: `handleCloseSyscall`
- **Arguments**:
  - `p0`: `int fd`
- **Return Value**: `0` on success, `-1` if not found.
- **Behavior**:
  - Removes the `FileDescriptor` from `thread->process->openFileHandles` and from `file->file_descriptors`.
  - Frees the stored path and descriptor structure.

### 33: `handleStatSyscall`
- **Arguments**:
  - `p0`: `int fd`
  - `p1`: `struct stat *buf` (user buffer pointer)
- **Return Value**: `0` on success, `-1` on error.
- **Behavior**:
  - Finds descriptor in `openFileHandles`.
  - Invokes `file->file_system->type->getattr(file, &buf, thread)`.
  - Copies `struct stat` to userland with `copy_from_kernel_to_process`.

### 34: `handleExecSyscall`
- **Arguments**:
  - `p0`: `int fd` (open file descriptor containing the ELF binary)
- **Return Value**: Does not return on success; `-1` on error.
- **Behavior**:
  - Validates `fd` from `openFileHandles`.
  - Clears all existing threads in the process.
  - Updates `process_files[PROC_FILE_EXECUTABLE]` with the new binary path.
  - Unmaps and frees all previous `virtual_memory_entries` (decrements physical refcounts and frees physical pages when refcount hits 0).
  - Reads the ELF file data into memory.
  - Calls `processLoadELF(process, file_data)`:
    - Parses ELF32 header and program headers (`PT_LOAD`).
    - Maps program segments (`MEM_TYPE_PROGRAM_DATA`).
    - Sets up an 8MB virtual stack (`MEM_TYPE_STACK`).
    - Builds initial stack frame: ELF auxiliary vectors (`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`), `argv`, `envp`, `argc`.
    - Creates the initial `ProcessThread` with `esp` and `eip` pointing to the ELF entry point.

### 35: `handleSetThreadPointerSyscall`
- **Arguments**:
  - `p0`: `uint32_t tp` (pointer to Thread Local Storage)
- **Return Value**: `0`.
- **Behavior**:
  - Stores `tp` into `thread->thread_pointer_gs`.
  - When returning to user mode, `processThread()` calls `set_thread_area_32(tp)`, which updates the GDT TLS entry (index 6, selector `0x33`) so `%gs:0` refers to the thread's TLS block.

---

## 5. How POSIX Operations are Handled Without Direct Syscalls

HoneyOS omits several standard POSIX syscalls by delegating them to the Virtual File System:

### Process Termination (`exit` / `exit_group`)
- **POSIX**: `exit(status)`
- **HoneyOS**:
  ```c
  void _exit_(int status) {
      int fd = _open_("/proc/self/status", O_WRONLY);
      _write_(fd, &status, 4);
      _close_(fd);
  }
  ```
  Writing 4 bytes to `/proc/self/status` triggers `process_exit(process, status)` in [`src/kernel/process/process.c`](file:///home/lukas/projects/honey-os-2/src/kernel/process/process.c#L204), terminating all threads, closing all file descriptors, and waking up any waiting parent.

### Process Waiting (`waitpid` / `wait4` / `waitid`)
- **POSIX**: `waitpid(pid, &status, ...)`
- **HoneyOS**:
  ```c
  int _wait4_(pid_t pid, int *status, int options, struct rusage *ru) {
      char filename[100];
      snprintf(filename, sizeof(filename) - 1, "/proc/%i/status", pid);
      int fd = _open_(filename, O_RDONLY);
      _read_(fd, status, 4);
      _close_(fd);
      return 0;
  }
  ```
  Reading 4 bytes from `/proc/<pid>/status` reads from a FIFO. If the process is still running, the calling thread blocks on the FIFO. When the child exits, it writes its exit code, waking up the reader.

### Heap Management (`brk`)
- **POSIX**: `brk(addr)`
- **HoneyOS**:
  In [`translate_call.c`](file:///home/lukas/projects/musl/src/internal/translate_call.c#L264-L281), `_brk_` always returns a fixed break address without growing it. Under Linux semantics, failing to grow the break causes `musl` libc to automatically route all `malloc()` allocations through `mmap()` (Syscall 30).

### Symlink Reading (`readlink` / `readlinkat`)
- **POSIX**: `readlink(path, buf, bufsiz)`
- **HoneyOS**:
  Instead of implementing a `readlink` syscall, HoneyOS adds `O_SYMLINK` (`0x1000`) to `open()`. Opening with `O_SYMLINK` causes `mountListFs` to return a descriptor to the symlink itself, and calling `read(fd, buf, bufsiz)` reads the destination target string directly.

*For full details on these and other POSIX differences, see [**`docs/posix-deviations.md`**](file:///home/lukas/projects/honey-os-2/docs/posix-deviations.md).*
