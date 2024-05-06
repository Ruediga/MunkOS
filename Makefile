override TOOL_DIR := tools
override RES_DIR := res
override KERNEL_DIR := kernel

override MAKEFLAGS += -rR

override IMAGE_NAME := image

override BASE_QEMU_ARGS := -M q35 -m 16G -enable-kvm -smp 16 -cpu "Nehalem"
override EXTRA_QEMU_ARGS := -monitor stdio -d int -M smm=off \
	-D error_log.txt -vga virtio -device pci-bridge,chassis_nr=2,id=b1 \
	-device pci-bridge,chassis_nr=3,id=b2 -serial file:serial_log.txt \
	#-no-reboot --no-shutdown #-trace *nvme*

.PHONY: all tools kernel run-iso-uefi run-img-bios run-img-uefi run-img-uefi-gdb

all: $(IMAGE_NAME).iso $(IMAGE_NAME).img

tools:
	$(MAKE) -C $(TOOL_DIR)

kernel:
	$(MAKE) -C $(KERNEL_DIR)/deps
	$(MAKE) -C $(KERNEL_DIR)

run-iso-bios: $(IMAGE_NAME).iso
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -cdrom $(IMAGE_NAME).iso -boot d

run-iso-uefi: tools $(IMAGE_NAME).iso
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -bios $(TOOL_DIR)/ovmf/OVMF.fd -cdrom $(IMAGE_NAME).iso -boot d

run-img-bios: tools $(IMAGE_NAME).img
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS)\
		-drive file=$(IMAGE_NAME).img,format=raw

run-img-uefi: tools $(IMAGE_NAME).img
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -bios $(TOOL_DIR)/ovmf/OVMF.fd\
		-drive file=$(IMAGE_NAME).img,format=raw,if=none,id=nvme_dev \
        -device nvme,drive=nvme_dev,serial=0

run-img-uefi-gdb: tools $(IMAGE_NAME).img
	qemu-system-x86_64 $(BASE_QEMU_ARGS) $(EXTRA_QEMU_ARGS) -bios $(TOOL_DIR)/ovmf/OVMF.fd\
		-drive file=$(IMAGE_NAME).img,format=raw,if=none,id=nvme_dev \
        -device nvme,drive=nvme_dev,serial=0 -s -S

# build the iso image
$(IMAGE_NAME).iso: tools kernel Makefile
	rm -rf iso_root
	mkdir -p iso_root
	cp -v $(KERNEL_DIR)/bin/kernel.elf_x86_64 \
		$(RES_DIR)/bg.jpg $(RES_DIR)/limine.cfg $(TOOL_DIR)/limine/limine-bios.sys \
		$(TOOL_DIR)/limine/limine-bios-cd.bin $(TOOL_DIR)/limine/limine-uefi-cd.bin iso_root/
	mkdir -p iso_root/EFI/BOOT
	cp -v $(TOOL_DIR)/limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v $(TOOL_DIR)/limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	$(TOOL_DIR)/limine/limine bios-install $(IMAGE_NAME).iso
	rm -rf iso_root

# build an image
$(IMAGE_NAME).img: tools kernel Makefile
	rm -f $(IMAGE_NAME).img
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).img
	sgdisk $(IMAGE_NAME).img -n 1:2048 -t 1:ef00
	$(TOOL_DIR)/limine/limine bios-install $(IMAGE_NAME).img
	mformat -i $(IMAGE_NAME).img@@1M
	mmd -i $(IMAGE_NAME).img@@1M ::/EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).img@@1M $(RES_DIR)/bg.jpg $(KERNEL_DIR)/bin/kernel.elf_x86_64 \
		$(RES_DIR)/limine.cfg $(TOOL_DIR)/limine/limine-bios.sys ::/
	mcopy -i $(IMAGE_NAME).img@@1M $(TOOL_DIR)/limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).img@@1M $(TOOL_DIR)/limine/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: clean
clean:
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).img serial_log.txt error_log.txt
	$(MAKE) -C $(TOOL_DIR) clean
	$(MAKE) -C $(KERNEL_DIR) clean

.PHONY: clean-full
clean-full: clean
	$(MAKE) -C $(TOOL_DIR) clean-full
	$(MAKE) -C $(KERNEL_DIR) clean-full
