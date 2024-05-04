# implement some config script thingy, e.g.:
# ADD_DEP { MunkOS }
# CLONE_CMD { "git clone https://github.com/Ruediga/MunkOS" }
# HEADERS { "kernel/include" }
# SOURCES { "kernel/src" }
# this could go ahead and clone the repo, and then copy all .c files
# inside SOURCES into deps/src/MunkOS and all .h files into
# deps/include/MunkOS, and then remove the cloned from files
# also add all the new included paths to gcc (-I)
# another idea would be to have a seperate Makefile per dependency
# since most projects rely on cmake (with ninja backend) or meson or similar

.PHONY: all

all: include/uacpi include/limine.h include/flanterm

flanterm:
	rm -rf flanterm
	@echo "> cloning newest version of flanterm, this MAY include breaking changes"
	git clone https://github.com/mintsuki/flanterm

include/flanterm: flanterm
	mkdir -p include/flanterm/backends
	cp flanterm/flanterm.h include/flanterm/
	cp flanterm/backends/fb.h include/flanterm/backends/
	mkdir -p src/flanterm/backends
	cp flanterm/flanterm.c src/flanterm/
	cp flanterm/backends/fb.c src/flanterm/backends/

include/limine.h:
	mkdir -p include
	curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

uACPI:
	rm -rf uACPI
	@echo "> cloning newest version of uACPI, this MAY include breaking changes"
	git clone https://github.com/UltraOS/uACPI

include/uacpi: uACPI
	mkdir -p include/uacpi/
	cp -r uACPI/include/uacpi/* include/uacpi/
	mkdir -p src/uacpi/
	cp -r uACPI/source/* src/uacpi/

.PHONY: clean
clean:
	rm -rf src include

.PHONY: clean-full
clean-full: clean
	rm -rf uACPI