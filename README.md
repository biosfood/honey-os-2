# Honey-OS 2 Build Instructions

This document provides instructions on how to set up the build environment and run the Honey-OS 2 project.

## Prerequisites

You need the following tools and packages to compile the OS:
- **Build tools**: `make`, `nasm`, `mtools`, `xorriso`, `grub-pc-bin`
- **Compilers**: `gcc-i686-linux-gnu`, `binutils-i686-linux-gnu`
- **Emulator**: `qemu-system-x86` (provides `qemu-system-i386`)
- **Rust toolchain**: Provide the `rust-src` component (via `rustup component add rust-src`) and make sure cargo is installed.
- **musl C library**: Ensure you have cloned the submodule `honey-os-musl` (`git submodule update --init --recursive`).

### Installing dependencies (Ubuntu/Debian)
```sh
sudo apt-get update
sudo apt-get install -y gcc-i686-linux-gnu binutils-i686-linux-gnu nasm mtools grub-pc-bin xorriso qemu-system-x86
```

### Compiler Aliases
The Makefile expects the cross-compiler to be available as `i686-elf-gcc`, `i686-elf-ar`, `i686-elf-ld`, and `i686-elf-ranlib`. Since Debian/Ubuntu packages these as `i686-linux-gnu-*`, you will need to create symbolic links:
```sh
sudo ln -s /usr/bin/i686-linux-gnu-gcc /usr/local/bin/i686-elf-gcc
sudo ln -s /usr/bin/i686-linux-gnu-ar /usr/local/bin/i686-elf-ar
sudo ln -s /usr/bin/i686-linux-gnu-ld /usr/local/bin/i686-elf-ld
sudo ln -s /usr/bin/i686-linux-gnu-ranlib /usr/local/bin/i686-elf-ranlib
```

## Building and Running

You can compile the system, standard library, and start the QEMU emulator with the default make target:
```sh
make run
```
**Note:** `make run` executes QEMU. If QEMU hangs and doesn't drop to a shell prompt after `hello-rust finished: 0`, the current workaround is to terminate it manually.
