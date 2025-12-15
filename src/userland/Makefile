CC = ../../build/musl/bin/musl-gcc
CCFLAGS = -m32 -mtune=generic -c -Wno-discarded-qualifiers -fms-extensions -Wno-shift-count-overflow -O0 -g
LD = ../../build/musl/bin/musl-gcc
LD_FLAGS = -z max-page-size=0x1000
AS = nasm
ASFlAGS = -felf32

SOURCE_FILES := $(shell find . -name "*.c" -or -name *.asm -or -name *.s)
OBJS := $(SOURCE_FILES:%=$(BUILD_FOLDER)/%.o)

PROGRAM_NAMES := $(shell ls -d */ | sed "s/\/$$//" | sed "s|build||")
PROGRAMS := $(PROGRAM_NAMES:%=../../initrd/bin/%)

default: ../../rootfs/initrd.tar

all: ../../rootfs/initrd.tar $(PROGRAMS)

../../rootfs/initrd.tar: $(PROGRAMS)
	@echo "packing files into rootfs/initrd.tar"
	@(cd ../../initrd && tar cf ../rootfs/initrd.tar --transform='s|^|/|' --show-transformed-names -v  *)

clean:
	@rm -r build

.SUFFIXES:

define MAKE_PROGRAM

../../initrd/bin/$(1): $(2) ../../build/musl/lib/libc.a
	@echo "linking $(1) from $(2)"
	@$(LD) $(LD_FLAGS) -o $$@ $(2)

build/$(1)/%.c.o: $(1)/%.c
	@echo "compiling '$$<' to '$$@' for $(1)"
	@mkdir -p $$(dir $$@)
	@$(CC) $(CCFLAGS) $$< -o $$@ -I $(1)/include

endef

$(foreach NAME, $(PROGRAM_NAMES), $(eval $(call MAKE_PROGRAM,$(NAME),$(shell find $(NAME) -name "*.c" | sed "s|$$|.o|" | sed "s|^|build/|"))))
