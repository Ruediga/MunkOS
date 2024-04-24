override MAKEFLAGS += -rR

override KERNEL_NAME := kernel.elf_x86_64

# replace this by a proper x86_64 toolchain
override CC := gcc
override LD := ld
override CFLAGS := -O3 -pipe -g
override NASMFLAGS := -F dwarf -g
override LDFLAGS :=

# c preprocessor flags
override CPPFLAGS := \
    -I include \
    -MMD \
    -MP

# c flags to simulate a properly configured cross compiler
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
	-fno-omit-frame-pointer \
    -Wno-unused-function \
    -DMUNKOS_DEBUG_BUILD \
    -DMUNKOS_VERBOSE_BUILD
    # built with extra debug checks: -DMUNKOS_DEBUG_BUILD
    # built with verbose printing: -DMUNKOS_VERBOSE_BUILD

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

# source files
override CFILES := $(shell cd src && find -type f -name '*.c')
override ASFILES := $(shell cd src && find -type f -name '*.S')
override NASMFILES := $(shell cd src && find -type f -name '*.asm')
# each source file will have an obj file
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
# find dependencies using gccs -MMD -MP
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

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
	python3 ../prepsymboltable.py bin/symboltable.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c bin/symboltable.c -o obj/symboltable.o
	$(LD) $(OBJ) $(LDFLAGS) obj/symboltable.o -o $@

-include $(HEADER_DEPS)

# c
obj/%.c.o: src/%.c GNUmakefile include/limine.h
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# gas
obj/%.S.o: src/%.S GNUmakefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# nasm
obj/%.asm.o: src/%.asm GNUmakefile
	mkdir -p "$$(dirname $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj

.PHONY: clean-full
clean-full: clean
	rm -f src/limine.h