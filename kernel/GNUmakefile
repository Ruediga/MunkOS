# Nuke built-in rules and variables.
override MAKEFLAGS += -rR

override KERNEL_NAME := kernel.elf_x86_64

# Convenience macro to reliably declare user overridable variables.
define DEFAULT_VAR =
    ifeq ($(origin $1),default)
        override $(1) := $(2)
    endif
    ifeq ($(origin $1),undefined)
        override $(1) := $(2)
    endif
endef

# It is suggested to use a custom built cross toolchain to build a kernel.
# We are using the standard "cc" here, it may work by using
# the host system's toolchain, but this is not guaranteed.
override DEFAULT_CC := gcc
$(eval $(call DEFAULT_VAR,CC,$(DEFAULT_CC)))

# Same thing for "ld" (the linker).
override DEFAULT_LD := ld
$(eval $(call DEFAULT_VAR,LD,$(DEFAULT_LD)))

# User controllable C flags.
override DEFAULT_CFLAGS := -O1 -pipe
$(eval $(call DEFAULT_VAR,CFLAGS,$(DEFAULT_CFLAGS)))

# User controllable C preprocessor flags. We set none by default.
override DEFAULT_CPPFLAGS :=
$(eval $(call DEFAULT_VAR,CPPFLAGS,$(DEFAULT_CPPFLAGS)))

# User controllable nasm flags.
override DEFAULT_NASMFLAGS := -F dwarf -g
$(eval $(call DEFAULT_VAR,NASMFLAGS,$(DEFAULT_NASMFLAGS)))

# User controllable linker flags. We set none by default.
override DEFAULT_LDFLAGS :=
$(eval $(call DEFAULT_VAR,LDFLAGS,$(DEFAULT_LDFLAGS)))

# Internal C flags that should not be changed by the user.
override CFLAGS += \
    -Wall \
    -Werror \
    -Wextra \
    -std=c11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIE \
    -m64 \
    -march=x86-64 \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -DMUNKOS_DEBUG_BUILD \
    -DMUNKOS_VERBOSE_BUILD \
	-fno-omit-frame-pointer \
    -Wno-unused-function
    # built for debug: -DMUNKOS_DEBUG_BUILD
    # built so verbose printing etc work: -DMUNKOS_VERBOSE_BUILD
	# -fno-omit-frame-pointer: for stacktrace, maintains frameptr even with > -00

# Internal C preprocessor flags that should not be changed by the user.
override CPPFLAGS := \
    -I include \
	-fno-omit-frame-pointer \
    $(CPPFLAGS) \
    -MMD \
    -MP

# Internal linker flags that should not be changed by the user.
override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -pie \
    --no-dynamic-linker \
    -z text \
    -z max-page-size=0x1000 \
    -T linker.ld

# Internal nasm flags that should not be changed by the user.
override NASMFLAGS += \
    -Wall \
    -f elf64

# Use "find" to glob all *.c, *.S, and *.asm files in the tree and obtain the
# object and header dependency file names.
override CFILES := $(shell cd src && find -L * -type f -name '*.c')
override ASFILES := $(shell cd src && find -L * -type f -name '*.S')
override NASMFILES := $(shell cd src && find -L * -type f -name '*.asm')
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

# Default target.
.PHONY: all
all: bin/$(KERNEL_NAME)

src/limine.h:
    curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

# generate the final kernel executable.
# for now, stack tracing is implemented by linking the kernel executable,
# extracting the required symbols from it, saving these into an array
# and finally recompiling with the symbols included
bin/$(KERNEL_NAME): GNUmakefile linker.ld $(OBJ)
	mkdir -p "$$(dirname $@)"
	$(LD) $(OBJ) $(LDFLAGS) -o $@
	python ../prepsymboltable.py bin/symboltable.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c bin/symboltable.c -o obj/symboltable.o
	$(LD) $(OBJ) $(LDFLAGS) obj/symboltable.o -o $@

# Include header dependencies.
-include $(HEADER_DEPS)

# Compilation rules for *.c files.
obj/%.c.o: src/%.c GNUmakefile src/limine.h
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compilation rules for *.S files.
obj/%.S.o: src/%.S GNUmakefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compilation rules for *.asm (nasm) files.
obj/%.asm.o: src/%.asm GNUmakefile
	mkdir -p "$$(dirname $@)"
	nasm $(NASMFLAGS) $< -o $@

# Remove object files and the final executable.
.PHONY: clean
clean:
	rm -rf bin obj

.PHONY: distclean
distclean: clean
	rm -f src/limine.h
