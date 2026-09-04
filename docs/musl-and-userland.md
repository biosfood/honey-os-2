# HoneyOS Musl Libc & Userland Architecture

This document covers `honey-os-musl`, how it is configured and built, how its translation shim bridges standard POSIX calls to HoneyOS, and how userland C and Rust programs/drivers operate.

---

## 1. HoneyOS Musl Location & Repository Structure

The C standard library for HoneyOS is a custom fork of `musl` libc:
- **Repository Location**: [`/home/lukas/projects/musl`](file:///home/lukas/projects/musl)
- **Git Remote Origin**: `git@github.com:biosfood/honey-os-musl.git`
- **Upstream**: `git://git.musl-libc.org/musl`
- **Submodule Configuration**: Defined in HoneyOS's [`.gitmodules`](file:///home/lukas/projects/honey-os-2/.gitmodules):
  ```ini
  [submodule "musl"]
      path = musl
      url = git@github.com:biosfood/honey-os-musl.git
  ```
- **Build Path**: Built out-of-tree into [`build/musl`](file:///home/lukas/projects/honey-os-2/build/musl). In the main [Makefile](file:///home/lukas/projects/honey-os-2/Makefile#L71-L85), it is configured from `build/musl` via the relative path `../../../musl`.

---

## 2. How `honey-os-musl` is Built

The build steps are defined in [`Makefile`](file:///home/lukas/projects/honey-os-2/Makefile#L70-L85):

```makefile
export CROSS_COMPILE=i386-elf-
MUSL_BUILD_DIR := $(abspath build/musl)

build/musl/config.mak: build
	cd build/musl && ../../../musl/configure \
		--target=i386-elf \
		--enable-debug \
		--disable-shared \
		--exec-prefix=$(MUSL_BUILD_DIR) \
		--prefix=$(MUSL_BUILD_DIR) \
		--syslibdir=$(MUSL_BUILD_DIR)

musl-lib: build/musl/config.mak
	cd build/musl && make -j8 && make install

build/musl/bin/musl-gcc: musl-lib
	cd build/musl &&\
	make obj/musl-gcc &&\
	make $(MUSL_BUILD_DIR)/bin/musl-gcc &&\
	make lib/musl-gcc.specs &&\
	sed -i 's/ crtbeginS.o%s//g; s/crtendS.o%s //g' -i lib/musl-gcc.specs &&\
	cp lib/libm.a lib/libg.a
```

### Build Artifacts Produced
- `build/musl/bin/musl-gcc`: The C compiler wrapper that automatically configures include paths, static linking, and musl specs.
- `build/musl/lib/libc.a`: The static C standard library.
- `build/musl/include/`: Header files providing standard POSIX APIs (`stdio.h`, `unistd.h`, `sys/stat.h`, `fcntl.h`, `pthread.h`, etc.).

---

## 3. The Syscall Translation Shim

Standard `musl` targets the Linux kernel ABI (`int 0x80` or `sysenter` with Linux `__NR_*` numbers). HoneyOS modifies musl to redirect all system calls through a userland translation shim:

```
POSIX C Library Call (e.g. open, fork, write, wait4, exit)
         |
         v
arch/i386/syscall_arch.h (__syscall0 ... __syscall6)
         |
         v
src/internal/translate_call.c (translate_call)
         |
         +--> Maps to VFS operations (/proc/self/status, /proc/<pid>/status)
         |
         +--> Maps to HoneyOS native syscalls via syscall_impl()
                    |
                    v
src/internal/i386/syscall_impl.s (sysenter)
```

### 1. Interception: `arch/i386/syscall_arch.h`
In [`/home/lukas/projects/musl/arch/i386/syscall_arch.h`](file:///home/lukas/projects/musl/arch/i386/syscall_arch.h):
```c
extern long translate_call(long n, long a1, long a2, long a3, long a4, long a5, long a6);

static inline long __syscall0(long n) {
    return translate_call(n, 0, 0, 0, 0, 0, 0);
}
static inline long __syscall1(long n, long a1) {
    return translate_call(n, a1, 0, 0, 0, 0, 0);
}
// ... __syscall2 through __syscall6 all forward to translate_call()
```

### 2. POSIX Mapping: `src/internal/translate_call.c`
In [`/home/lukas/projects/musl/src/internal/translate_call.c`](file:///home/lukas/projects/musl/src/internal/translate_call.c):

| POSIX Call | Action / HoneyOS Mapping |
|---|---|
| `SYS_open` / `SYS_openat` | Calls `_open_()`. If `O_CREAT` is set and file doesn't exist, calls `_create_()` first. Uses `HONEY_SYS_OPEN`. |
| `SYS_read` | `_read_()` -> `HONEY_SYS_READ` |
| `SYS_write` / `SYS_writev` | `_write_()` -> `HONEY_SYS_WRITE` |
| `SYS_close` | `_close_()` -> `HONEY_SYS_CLOSE` |
| `SYS_fork` | `_fork_()` -> `HONEY_SYS_FORK` |
| `SYS_execve` | `_execve_()` -> opens the target path with `open()` and calls `HONEY_SYS_EXEC` with the resulting file descriptor. |
| `SYS_mmap` / `SYS_mmap2` | `_mmap_()` -> packs arguments into `MmapArgs` and calls `HONEY_SYS_MMAP`. |
| `SYS_munmap` | `_munmap_()` -> `HONEY_SYS_MUNMAP` |
| `SYS_mkdir` | `_mkdir_()` -> opens parent directory and calls `HONEY_SYS_FILE_CREATE` with `FILE_TYPE_DIRECTORY`. |
| `SYS_mknod` | If `mode & S_IFIFO`, calls `_create_()` with `FILE_TYPE_FIFO`. |
| `SYS_fstat` / `SYS_statx` | `_fstat_()` -> `HONEY_SYS_STAT` |
| `SYS_pipe` / `SYS_pipe2` | `_pipe_()` / `_pipe2_()`: opens `/kernel/pipe` (`O_RDONLY`), then opens `/proc/self/fd/<rfd>` (`O_WRONLY`), returning reader and writer handles. |
| `SYS_dup` / `SYS_dup2` / `SYS_dup3` | `_dup_()` / `_dup2_()` / `_dup3_()`: duplicates descriptor by opening `/proc/self/fd/<oldfd>` with tracked access flags (and for `dup2`/`dup3`, incrementally allocating descriptors to target `newfd`). |
| `SYS_set_thread_area` | Calls `syscall_impl(HONEY_SYS_SET_GP, a1, 0, 0, 0)` |
| `SYS_get_thread_area` | Returns `%gs:0` via inline assembly. |
| `SYS_exit` / `SYS_exit_group` | `_exit_()`: Opens `/proc/self/status` and writes 4-byte exit status. |
| `SYS_wait4` / `SYS_waitid` | `_wait4_()`: Opens `/proc/<pid>/status` and reads 4-byte exit status (blocks if child running). |
| `SYS_brk` | `_brk_()`: Returns fixed `_end` address. Refuses heap expansion, forcing musl to use `mmap` for all heap allocations. |

### 3. Low-Level `sysenter` Assembly: `src/internal/i386/syscall_impl.s`
In [`/home/lukas/projects/musl/src/internal/i386/syscall_impl.s`](file:///home/lukas/projects/musl/src/internal/i386/syscall_impl.s):
- Preserves all caller registers (`pusha`) and saves 8 SSE registers (`xmm0`-`xmm7`, 128 bytes) onto the stack.
- Loads arguments into registers (`eax` = function, `ebx` = p0, `ecx` = p1, `edx` = p2, `esi` = p3).
- Sets `edi = esp` and pushes the return label `$end`.
- Executes `sysenter`.
- Upon `sysexit` from the kernel, restores SSE registers and `popa`, returning `eax`.

---

## 4. Userland Software Stack

Userland binaries are divided into two categories:

### C Userland (`src/userland/`)
- **Build System**: [`src/userland/Makefile`](file:///home/lukas/projects/honey-os-2/src/userland/Makefile)
- Uses `../../build/musl/bin/musl-gcc` to compile and link each subdirectory into a static binary in `../../initrd/bin/<name>`.
- Programs:
  - `init`: First process spawned by the kernel. Creates `/dev/serial/*` and `/dev/tty1/*` FIFOs, redirects stdin/stdout, and forks userland service daemons.
  - `pty`: Pseudo-terminal daemon connecting TTY FIFOs.
  - `sh`: Interactive command-line shell.
  - `pic`: Programmable Interrupt Controller driver.
  - `pit`: Programmable Interval Timer driver.
  - `index-pci`: Enumerates PCI devices.

### Rust Userland (`src/userland-rust/`)
- **Build System**: [`src/userland-rust/Makefile`](file:///home/lukas/projects/honey-os-2/src/userland-rust/Makefile)
- Uses `cargo +nightly build -Z build-std=std,panic_abort --target i686-unknown-linux-musl --release`.
- Links against HoneyOS `build/musl/lib` using `musl-gcc` and generates a stub `libunwind.a` to satisfy Rust backtraces.
- Programs:
  - `hello-rust`: Verifies Rust runtime, allocations, and output.
  - `pty`: Rust-based PTY implementation.
  - `serial`: Serial port driver written in Rust.

---

## 5. How Userland Drivers Work in HoneyOS

Because HoneyOS exposes hardware via the VFS, userland drivers are ordinary processes with no special syscall privileges:

### Example: The PIC Driver ([`src/userland/pic/main.c`](file:///home/lukas/projects/honey-os-2/src/userland/pic/main.c))
1. **Control Ports**: Opens `/dev/port/0x20` (master command) and `/dev/port/0xA0` (slave command).
2. **Interrupt Subscriptions**: Opens `/dev/interrupt/<32 + i>` to listen for hardware IRQs.
3. **Driver Exposure**: Creates and opens named FIFOs `/dev/pic/<irq>` (e.g. `/dev/pic/1` for keyboard).
4. **Event Loop**:
   - Blocks on `read(interrupt_fd, &buf, 1)`.
   - When an IRQ fires, the kernel writes a byte to `/dev/interrupt/<irq>`, unblocking the driver.
   - The driver queries the PIC In-Service Register (ISR) by writing command `0x0B` to the command port and reading the response.
   - Dispatches the event to registered clients by writing `'1'` to `/dev/pic/<irq>`.
   - Sends End-Of-Interrupt (EOI: `0x20`) to the PIC command ports.

---

## 6. Exact Git Changelog from Stock Musl

Every commit in `honey-os-musl` (`/home/lukas/projects/musl`) since cloning from upstream `git://git.musl-libc.org/musl` (commit `0b86d60`):

| Commit Hash | Commit Message | Key Changes |
|---|---|---|
| `73de7ea` | `remove unneeded files` | Stripped unnecessary arch/platform files |
| `b925aa5` | `remove args` | Cleaned up unused argument handling |
| `03fbf21` | `translate syscalls for honey: basic setup` | Initial `translate_call.c` and redirection hook in `arch/i386/syscall_arch.h` |
| `9cd8a4f` | `remove som features to make a hello world program work` | Minimal runtime trimming for early bringup |
| `35d05f5` | `readd __init_tls` | Re-enabled TLS initialization with `#define SYSCALL_NO_TLS 1` in `src/env/__init_tls.c` |
| `7fafafe` | `remove argument mocking` | Removed test mocks in favor of real kernel invocation |
| `9312c1d` | `syscall_impl.s: use pusha instead of manual push and pop` | Compact register save/restore using `pusha`/`popa` |
| `56dc3f4` | `add common system call implementations` | Added core translations in `translate_call.c` (`open`, `read`, `write`, `fork`, `execve`, `mmap`, `munmap`) |
| `65425b5` | `add O_SYMLINK flag` | Added `#define O_SYMLINK 0x1000` to `include/fcntl.h` |
| `50a163e` / `c41480a` | `move statx struct to sys/stat.h` / `add stat calls` | Added `struct statx` to `include/sys/stat.h` and hooked `SYS_fstat`/`SYS_statx` in `translate_call.c` |
| `015d78a` | `use honey-os-2 set_thread_area system call` | Mapped `SYS_set_thread_area` to `HONEY_SYS_SET_GP` (35) and `SYS_get_thread_area` to `movl %gs:0` |
| `0fb5dae` | `correct pthread_create` | Fixed thread parameter passing and stack pointer setup |
| `973b1b7` | `add call translations for exit() and wait()` | Routed `exit()` -> write to `/proc/self/status` and `wait4()`/`waitid()` -> read from `/proc/<pid>/status` |
| `a60e17c` | `fix open flags in translate_call.c` | Corrected flag propagation in `_open_()` |
| `478917f` / `f765583` | `adapt pthread_create.c and pthread_join.c for honey-os-2` / `fix pthread_create and join` | Replaced Linux `__clone` with `honeyos_pthread_create()`; `__pthread_exit` writes result to `/proc/self/threads/self/status`; `pthread_join` reads from `/proc/self/threads/<tid>/status` |
| `4bd1539` | `add SYS_brk stub` | Added `_brk_` returning constant `_end` boundary to force `musl` malloc onto `mmap` |
| `5091ed4` | `allow SSE instructions: stack alignment and save registers` | Saved/restored `xmm0`-`xmm7` (128 bytes) across syscalls in `syscall_impl.s`, and ensured 16-byte stack alignment in `pthread_create.c` and `crt_arch.h` |

### Summary of Every Modified File in `honey-os-musl`:
1. **[`arch/i386/syscall_arch.h`](file:///home/lukas/projects/musl/arch/i386/syscall_arch.h)**:
   - Replaced inline Linux `int $128` syscall dispatch with `translate_call(n, a1, a2, a3, a4, a5, a6)` for all `__syscall0` through `__syscall6`.
2. **[`src/internal/translate_call.c`](file:///home/lukas/projects/musl/src/internal/translate_call.c)** *(NEW FILE)*:
   - Maps Linux `SYS_*` numbers to HoneyOS native syscalls (`HONEY_SYS_*`) via `syscall_impl()`.
   - Translates `pipe()` and `pipe2()` into opening `/kernel/pipe` and duplicating via `/proc/self/fd/<id>`.
   - Translates `dup()`, `dup2()`, and `dup3()` into opening `/proc/self/fd/<oldfd>` with preserved descriptor access modes.
   - Translates `exit()` and `exit_group()` into writing to `/proc/self/status`.
   - Translates `wait4()` and `waitid()` into reading from `/proc/<pid>/status`.
   - Implements `_brk_()` stub forcing heap expansion failure so musl uses `mmap()`.
3. **[`src/internal/i386/syscall_impl.s`](file:///home/lukas/projects/musl/src/internal/i386/syscall_impl.s)** *(NEW FILE)*:
   - Low-level `sysenter` invocation saving `pusha` and 128 bytes of SSE registers (`xmm0`-`xmm7`).
4. **[`src/thread/pthread_create.c`](file:///home/lukas/projects/musl/src/thread/pthread_create.c)**:
   - Replaced Linux `__clone` syscall with `honeyos_pthread_create(stack, TP_ADJ(new))` (Syscall 25).
   - Enforced 16-byte stack alignment for SSE instructions.
   - Modified `__pthread_exit()` to write the exit pointer to `/proc/self/threads/self/status`.
5. **[`src/thread/pthread_join.c`](file:///home/lukas/projects/musl/src/thread/pthread_join.c)**:
   - Replaced futex wait with reading 4 bytes from `/proc/self/threads/<tid>/status` to block until the thread terminates.
6. **[`include/fcntl.h`](file:///home/lukas/projects/musl/include/fcntl.h)**:
   - Added `#define O_SYMLINK 0x1000` for opening symlinks without following them.
7. **[`include/sys/stat.h`](file:///home/lukas/projects/musl/include/sys/stat.h)**:
   - Added `struct statx` and `struct statx_timestamp` definitions.
8. **[`arch/i386/crt_arch.h`](file:///home/lukas/projects/musl/arch/i386/crt_arch.h)**:
   - Added `and $-16, %esp` to guarantee 16-byte stack alignment at process entry.

*For full details on C library and architectural deviations from standard POSIX, see [**`docs/posix-deviations.md`**](file:///home/lukas/projects/honey-os-2/docs/posix-deviations.md).*

