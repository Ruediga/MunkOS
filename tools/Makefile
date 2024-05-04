# build ./limine/limine executable to strap limine binaries onto the binary
override HOST_CC := cc
override HOST_CFLAGS := -O3
override HOST_CPPFLAGS := -O3
override HOST_LDFLAGS :=
override HOST_LIBS :=

.PHONY: all clean

all: limine ovmf

limine:
	rm -rf limine
	git clone https://github.com/limine-bootloader/limine.git --branch=v6.x-branch-binary --depth=1
	$(MAKE) -C limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

ovmf:
	mkdir -p ovmf
	cd ovmf && curl -Lo OVMF.fd https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd

clean:
	rm -rf limine

clean-full: clean
	rm -rf ovmf