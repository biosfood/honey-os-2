IMAGE_FILE = /run/user/1000/honey-os.img

export CC=i686-elf-gcc
export AR=i686-elf-ar
export RANLIB=i686-elf-ranlib
CCFLAGS = -m32 -mtune=generic -ffreestanding -nostdlib -c -I src/include -I src/kernel/include -Wno-discarded-qualifiers -fms-extensions -Wno-shift-count-overflow -O0 -g -Ibuild/musl/include -std=gnu11
LD = i686-elf-ld
LD_FLAGS = -z max-page-size=0x1000 -T link.ld
AS = nasm
ASFlAGS = -felf32 -w-zeroing
EMU = qemu-system-i386
EMUFLAGS = -m 1G -drive if=none,id=stick,format=raw,file=$(IMAGE_FILE) -no-reboot -no-shutdown -monitor unix:qemu-monitor-socket,server,nowait -serial stdio -d int -D crashlog.log -d int -device qemu-xhci -device usb-mouse -device usb-storage,drive=stick -device usb-kbd -gdb tcp::9000

BUILD_FOLDER = build

SOURCE_FILES := $(shell find src/kernel -name *.c -or -name *.asm -or -name *.s)
OBJS := $(SOURCE_FILES:%=$(BUILD_FOLDER)/%.o)

run: build $(IMAGE_FILE)
	@echo "starting qemu"
	@$(EMU) $(EMUFLAGS)

all: run

build:
	@mkdir build
	@mkdir build/musl

initrd:
	@mkdir -p initrd/bin initrd/etc
	@cp etc/devd.rules initrd/etc/devd.rules


$(IMAGE_FILE): rootfs/boot/kernel rootfs/initrd.tar
	@echo "creating the iso image"
	@grub-mkrescue -o $(IMAGE_FILE) rootfs

rootfs/boot/kernel: $(OBJS) link.ld
	@echo "linking"
	@$(LD) $(LD_FLAGS) -o $@ $(OBJS)

$(BUILD_FOLDER)/%.asm.o: %.asm
	@echo "asembling $<"
	@mkdir -p $(dir $@)
	@$(AS) $(ASFlAGS) $< -o $@

$(BUILD_FOLDER)/%.c.o: %.c build/musl/bin/musl-gcc
	@echo "compiling $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CCFLAGS) -r $< -o $@

$(BUILD_FOLDER)/%.s.o: %.s
	@echo "assembling $<" 
	@mkdir -p $(dir $@)
	@$(CC) $(CCFLAGS) -r $< -o $@

userPrograms: initrd build/musl/bin/musl-gcc
	@echo 'making user programs'
	@make --silent -C src/userland
	@echo 'making rust user programs'
	@make --silent -C src/userland-rust

rootfs/initrd.tar: userPrograms
	@echo "packing files into rootfs/initrd.tar"
	@(cd initrd && tar cf ../rootfs/initrd.tar --transform='s|^|/|' --show-transformed-names -v  *)

clean:
	@echo "clearing build folder"
	@rm -r $(BUILD_FOLDER) initrd src/userland/build

export CROSS_COMPILE=i386-elf-
MUSL_BUILD_DIR := $(abspath build/musl)
build/musl/config.mak: build
	cd build/musl && ../../../musl/configure --target=i386-elf --enable-debug --disable-shared --exec-prefix=$(MUSL_BUILD_DIR) --prefix=$(MUSL_BUILD_DIR) --syslibdir=$(MUSL_BUILD_DIR)

musl-lib: build/musl/config.mak
	cd build/musl && make -j8 && make install

build/musl/bin/musl-gcc: musl-lib
	cd build/musl &&\
	make obj/musl-gcc &&\
	make $(MUSL_BUILD_DIR)/bin/musl-gcc &&\
	make lib/musl-gcc.specs &&\
	sed -i 's/ crtbeginS.o%s//g; s/crtendS.o%s //g' -i lib/musl-gcc.specs &&\
	cp lib/libm.a lib/libg.a
