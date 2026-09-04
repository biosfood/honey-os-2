# HoneyOS Architecture & Developer Documentation

Welcome to the HoneyOS documentation. This directory provides detailed guides and technical references for understanding and developing on HoneyOS.

## Quick Index

- [**System Calls (`docs/syscalls.md`)**](file:///home/lukas/projects/honey-os-2/docs/syscalls.md): Complete reference for every system call in HoneyOS, the low-level `sysenter`/`sysexit` entry/exit mechanisms, calling conventions, and how POSIX calls (including `exit` and `wait4`) are handled.
- [**Virtual File System & Filesystems (`docs/vfs-and-filesystems.md`)**](file:///home/lukas/projects/honey-os-2/docs/vfs-and-filesystems.md): Breakdown of the VFS architecture, the mount router (`mountListFs`), and all in-kernel filesystems (`ramfs`, `kernelfs`, `procfs`, `/dev/cpuid`, `/dev/port`, `/dev/interrupt`, and `fifo`).
- [**HoneyOS Musl & Userland (`docs/musl-and-userland.md`)**](file:///home/lukas/projects/honey-os-2/docs/musl-and-userland.md): Where `honey-os-musl` lives, exact git commit changelog from stock musl, how it is built into `musl-gcc`, how the POSIX translation shim works (`translate_call.c` and `syscall_impl.s`), userland C and Rust builds, and userland hardware drivers.
- [**Deviations from POSIX (`docs/posix-deviations.md`)**](file:///home/lukas/projects/honey-os-2/docs/posix-deviations.md): Detailed documentation on `O_SYMLINK`, why no `readlink()` syscall is needed, and how POSIX operations (process/thread exit & wait, `brk`, anonymous pipes) map to file operations.

---

## High-Level Architecture Overview

HoneyOS is a 32-bit x86 operating system adhering closely to the **microkernel / hybrid-kernel UNIX philosophy**: *"Everything is a file"*.

```
+-------------------------------------------------------------------------+
|                              USERLAND                                   |
|                                                                         |
|  +-------------------+  +-------------------+  +---------------------+  |
|  | C Programs (init, |  | Rust Binaries     |  | Userland Drivers    |  |
|  | sh, index-pci...) |  | (hello-rust, pty) |  | (pic, pit, serial)  |  |
|  +---------+---------+  +---------+---------+  +----------+----------+  |
|            |                      |                       |             |
|            +----------------------+-----------------------+             |
|                                   |                                     |
|                      honey-os-musl (C Library)                          |
|             translate_call.c + sysenter (syscall_impl.s)                 |
+-----------------------------------+-------------------------------------+
                                    | SYSENTER / SYSEXIT
+-----------------------------------+-------------------------------------+
|                           KERNEL SPACE                                  |
|                                                                         |
|  +-------------------------------------------------------------------+  |
|  | Syscall Dispatcher (src/kernel/syscalls/syscall.c)                |  |
|  | 12 Native Syscalls (fork, open, read, write, mmap, exec...)       |  |
|  +---------------------------------+---------------------------------+  |
|                                    |                                    |
|  +---------------------------------v---------------------------------+  |
|  | VFS Mount Router (mountListFs)                                    |  |
|  +----+--------------+--------------+-------------+---------------+--+  |
|       |              |              |             |               |     |
|   +---v---+      +---v---+      +---v---+     +---v---+       +---v---+ |
|   | ramfs |      | procfs|      |kernel |     | fifo  |       |dev fs | |
|   |  (/)  |      |(/proc)|      |  fs   |     | IPC   |       |ports, | |
|   +-------+      +-------+      +-------+     +-------+       |irq... | |
+---------------------------------------------------------------+---------+
```

### Key Architectural Characteristics

1. **Hardware Access via File Operations**:
   - I/O Ports: Reading and writing to x86 I/O ports (`in`/`out`) is performed by reading and writing to files under `/dev/port/<port_num>` (`port_file_system`).
   - Hardware Interrupts: IRQs are exposed as FIFO files under `/dev/interrupt/<irq_num>`. Interrupt service routines in the kernel write a trigger byte into the corresponding FIFO, waking up userland driver threads that are blocked reading from them.
   - CPU Information: CPUID queries are performed by reading 16 bytes from `/dev/cpuid/<leaf_id>`.

2. **Process Lifecycle via `/proc`**:
   - Instead of dedicated syscalls for process termination (`exit`) and waiting (`wait4`/`waitpid`), userland interacts with `/proc`:
     - Exiting a process: Writing the 4-byte exit status integer to `/proc/self/status`.
     - Exiting a thread: Writing the 4-byte return value to `/proc/<pid>/threads/<tid>/status`.
     - Waiting for a process: Reading 4 bytes from `/proc/<pid>/status` (blocks on a FIFO until the child process exits).

3. **Memory Management & Copy-On-Write (COW)**:
   - Higher-half kernel mapped at virtual `0xFF800000` / `0xFFC00000`. Kernel page directory is located at physical `0x500000`.
   - `fork` creates COW memory mappings: parent and child page table entries are marked read-only with `copy_on_write = true` and `refcount` incremented.
   - Page fault handler (`onException`, `intNo == 0x0E && errorCode == 7`) detects userland write violations on COW pages, clones the physical frame if `refcount > 1`, and updates page tables to be writable.

4. **Boot Sequence**:
   1. GRUB boots the kernel ELF (`rootfs/boot/kernel`) and loads `rootfs/initrd.tar` via Multiboot 2.
   2. Kernel initializes higher-half paging, enables FPU & SSE (`cr0`, `cr4`, `fninit`), sets up physical page allocations, and sets up GDT and IDT (`boot.asm`, `main.c`).
   3. Kernel creates the root mount router (`mountListFs`), mounts `ramfs` at `/`, `kernelfs` at `/kernel/`, `/dev/cpuid/`, `/dev/port/`, and `/dev/interrupt/`.
   4. Unpacks `initrd.tar` into `ramfs` via `processInitrd` (`multiboot/tar.c`).
   5. Creates container, mounts `procfs` at `/proc/`, loads `/bin/init` ELF binary, allocates stdin/stdout/stderr pointing to `/kernel/null`.
   6. Configures SYSENTER MSR registers (`0x174`-`0x176`) and enters the cooperative scheduler loop.
   7. `/bin/init` spawns the userland drivers (`/bin/pty`, `/bin/pic`, `/bin/serial`, `/bin/pit`), sets up TTY FIFOs, and launches shells and programs.
