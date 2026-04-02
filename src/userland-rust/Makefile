CARGO = cargo
CC = ../../build/musl/bin/musl-gcc
LD_FLAGS = -m32 -z max-page-size=0x1000

PROGRAM_NAMES := hello-rust
PROGRAMS := $(PROGRAM_NAMES:%=../../initrd/bin/%)

default: all

all: $(PROGRAMS)

clean:
	@for dir in $(PROGRAM_NAMES); do \
		(cd $$dir && $(CARGO) clean); \
	done

define MAKE_RUST_PROGRAM

../../initrd/bin/$(1): $(1)/target/i686-unknown-linux-musl/release/lib$(subst -,_,$(1)).a
	@echo "linking rust program $(1)"
	@mkdir -p $$(dir $$@)
	@$$(CC) $$(LD_FLAGS) -o $$@ $$<

$(1)/target/i686-unknown-linux-musl/release/lib$(subst -,_,$(1)).a:
	@echo "compiling rust program $(1)"
	@cd $(1) && $$(CARGO) build --target i686-unknown-linux-musl --release

endef

$(foreach NAME, $(PROGRAM_NAMES), $(eval $(call MAKE_RUST_PROGRAM,$(NAME))))

.PHONY: all clean default
