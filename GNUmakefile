override MAKEFLAGS += -rR

override IMAGE_NAME := image

override BASE_QEMU_ARGS := -M q35 -m 4G -enable-kvm -cpu host -smp 4
override EXTRA_QEMU_ARGS := -monitor stdio -d int -M smm=off \
	-D error_log.txt -vga virtio -device pci-bridge,chassis_nr=2,id=b1 \
	-device pci-bridge,chassis_nr=3,id=b2 -serial file:serial_log.txt \
	#-trace *nvme*


# build ./limine/limine executable to strap limine onto the binary
override HOST_CC := cc
override HOST_CFLAGS := -g -O2 -pipe
override HOST_CPPFLAGS :=
override HOST_LDFLAGS :=
override HOST_LIBS :=

.PHONY: all
all: ovmf $(IMAGE_NAME).iso $(IMAGE_NAME).img

.PHONY: run-iso-bios
run-iso-bios: $(IMAGE_NAME).iso
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -cdrom $(IMAGE_NAME).iso -boot d

.PHONY: run-iso-uefi
run-iso-uefi: ovmf $(IMAGE_NAME).iso
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -bios ovmf/OVMF.fd -cdrom $(IMAGE_NAME).iso -boot d

.PHONY: run-img-bios
run-img-bios: $(IMAGE_NAME).img
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS)\
		-drive file=$(IMAGE_NAME).img,format=raw

.PHONY: run-img-uefi
run-img-uefi: ovmf $(IMAGE_NAME).img
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -bios ovmf/OVMF.fd\
		-drive file=$(IMAGE_NAME).img,format=raw,if=none,id=nvme_dev \
        -device nvme,drive=nvme_dev,serial=0

ovmf:
	mkdir -p ovmf
	cd ovmf && curl -Lo OVMF.fd https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v5.x-branch-binary --depth=1
	$(MAKE) -C limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

# build the iso image
$(IMAGE_NAME).iso: limine kernel
	rm -rf iso_root
	mkdir -p iso_root
	cp -v kernel/bin/kernel.elf_x86_64 \
		 bg.jpg limine.cfg limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./limine/limine bios-install $(IMAGE_NAME).iso
	rm -rf iso_root

# build a real image
$(IMAGE_NAME).img: limine kernel
	rm -f $(IMAGE_NAME).img
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).img
	sgdisk $(IMAGE_NAME).img -n 1:2048 -t 1:ef00
	./limine/limine bios-install $(IMAGE_NAME).img
	mformat -i $(IMAGE_NAME).img@@1M
	mmd -i $(IMAGE_NAME).img@@1M ::/EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).img@@1M bg.jpg kernel/bin/kernel.elf_x86_64 limine.cfg limine/limine-bios.sys ::/
	mcopy -i $(IMAGE_NAME).img@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).img@@1M limine/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: clean
clean:
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).img
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean: clean
	rm -rf limine ovmf
	$(MAKE) -C kernel distclean
