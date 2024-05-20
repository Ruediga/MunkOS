# lets just use cmake

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
	#cp -r uACPI/include/uacpi/* include/uacpi/
	mkdir -p src/uacpi/
	#cp -r uACPI/source/* src/uacpi/

.PHONY: clean
clean:
	rm -rf src include

.PHONY: clean-full
clean-full: clean
	rm -rf uACPI