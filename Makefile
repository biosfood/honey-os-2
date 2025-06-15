IMAGE_FILE = /run/user/1000/honey-os.img

CC = i686-elf-gcc
CCFLAGS = -m32 -mtune=generic -ffreestanding -nostdlib -c -I src/include -I src/kernel/include -Wno-discarded-qualifiers -fms-extensions -Wno-shift-count-overflow -O0 -g
LD = i686-elf-ld
LD_FLAGS = -z max-page-size=0x1000 -T link.ld
AS = nasm
ASFlAGS = -felf32
EMU = qemu-system-i386
EMUFLAGS = -m 1G -drive if=none,id=stick,format=raw,file=$(IMAGE_FILE) -no-reboot -no-shutdown -monitor unix:qemu-monitor-socket,server,nowait -parallel stdio -d int -D crashlog.log -d int -device qemu-xhci -device usb-mouse -device usb-storage,drive=stick -device usb-kbd -gdb tcp::9000

BUILD_FOLDER = build

SOURCE_FILES := $(shell find src/kernel -name *.c -or -name *.asm -or -name *.s)
OBJS := $(SOURCE_FILES:%=$(BUILD_FOLDER)/%.o)

run: build hlib userPrograms $(IMAGE_FILE)
	@echo "starting qemu"
	@$(EMU) $(EMUFLAGS)

all: run

build:
	@mkdir build

initrd:
	@mkdir initrd
	@mkdir initrd/bin

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

$(BUILD_FOLDER)/%.c.o: %.c
	@echo "compiling $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CCFLAGS) -r $< -o $@

$(BUILD_FOLDER)/%.s.o: %.s
	@echo "assembling $<" 
	@mkdir -p $(dir $@)
	@$(CC) $(CCFLAGS) -r $< -o $@

userPrograms: initrd
	@echo 'making user programs'
	@make --silent -C src/userland

hlib: initrd
	@make --silent -C src/hlib

clean:
	@echo "clearing build folder"
	@rm -r $(BUILD_FOLDER) initrd src/userland/build
