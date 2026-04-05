CARGO = cargo
CARGO_CMD = $(CARGO) +nightly
CC = $(abspath ../../build/musl/bin/musl-gcc)
LD_FLAGS = -m32 -z max-page-size=0x1000

PROGRAM_NAMES := $(shell ls -d */ | sed "s/\/$$//" | sed "s|build||")
PROGRAMS := $(PROGRAM_NAMES:%=../../initrd/bin/%)
MUSL_LIB_DIR = $(abspath ../../build/musl/lib)
GCC_LIB_DIR := $(dir $(shell $(CC) -print-libgcc-file-name))

export CARGO_TARGET_I686_UNKNOWN_LINUX_MUSL_LINKER = $(CC)

export CARGO_TARGET_I686_UNKNOWN_LINUX_MUSL_RUSTFLAGS = \
	-C link-self-contained=no \
	-C link-arg=-B$(MUSL_LIB_DIR) \
	-C link-arg=-B$(GCC_LIB_DIR) \
	-L native=$(MUSL_LIB_DIR) \
	-C link-arg=-m32 \
	-C link-arg=-z \
	-C link-arg=max-page-size=0x1000 \
	-C relocation-model=static

export CC_i686_unknown_linux_musl = $(CC)
default: all

all: $(MUSL_LIB_DIR)/libunwind.a $(PROGRAMS)

clean:
	@for dir in $(PROGRAM_NAMES); do \
		(cd $$dir && $(CARGO) clean); \
	done

$(MUSL_LIB_DIR)/libunwind.a:
	@echo "Creating dummy libunwind.a with stubbed symbols to satisfy Rust backtraces..."
	@echo 'int _Unwind_Backtrace(void *a, void *b) { return 5; }' > /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetIP(void *context) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetDataRelBase(void *c) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetLanguageSpecificData(void *c) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetRegionStart(void *c) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetTextRelBase(void *c) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_SetGR(void *c, int i, unsigned long n) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_SetIP(void *c, unsigned long i) { return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_Resume(void *e) { while(1); return 0; }' >> /tmp/dummy_unwind.c
	@echo 'unsigned long _Unwind_GetCFA(void *c) { return 0; }' >> /tmp/dummy_unwind.c
	@$(CC) -c -x c /tmp/dummy_unwind.c -o /tmp/dummy_unwind.o
	@ar rcs $@ /tmp/dummy_unwind.o
	@rm /tmp/dummy_unwind.c /tmp/dummy_unwind.o

FORCE: ;

define MAKE_RUST_PROGRAM
../../initrd/bin/$(1): FORCE
	@echo "Compiling and linking rust program $(1) via Cargo"
	@cd $(1) && $$(CARGO_CMD) build \
		-Z build-std=std,panic_abort \
		--target i686-unknown-linux-musl \
		--release
	@echo "Copying rust program $(1) to initrd"
	@mkdir -p $$(dir $$@)
	@cp $(1)/target/i686-unknown-linux-musl/release/$(1) ../../initrd/bin/$(1)
endef

$(foreach NAME, $(PROGRAM_NAMES), $(eval $(call MAKE_RUST_PROGRAM,$(NAME))))

.PHONY: all clean default
