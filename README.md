# MunkOS

Operating System very early on in dev for learning purposes with simplicity in mind.

Currently, MunkOS only supports the x86_64 architecture, but support for other architectures is very likely to be added at some point. The OS boots up with the Limine-Bootloader, which can be found at https://www.github.com/limine-bootloader/limine.

# Building

It is recommended to use a Linux distro (WSL should work too) to build from source yourself. Building requires the following packages which you can install with your distros package manager (apt, pacman, ...):

[TODO] finish list, script for cross compiler toolchain
> sudo pacman -S gcc build-essential make xorriso

1) Download dependencies by running ./install-dep.sh
2) 
```sh
# compile everything
make build
# build image
make image
# run img with qemu
make run
```

### Dependencies

- [Limine](https://www.github.com/limine-bootloader/limine)
- [printf](https://github.com/mpaland/printf) (Agenda: remove dependency)
- [flanterm](https://github.com/mintsuki/flanterm)

# Goals

- [x] PMM
- [ ] VMM
- [x] GDT
- [x] Interrupts
- [ ] Timer
- [ ] Threads
- [ ] Scheduler

### Long Term

I MunkOS to be a completely dependency free OS, so writing a standard library for C, system library and replacing pretty much every third party software with my own is something I intend to achieve. Porting Linux software, implementing a GUI and writing an interpreted language for making user space applications are some very far, but reachable goals I have had in mind since the beginning.



<!--
If you want to know what this down below is, you found the Limine barebones readme

## How to use this?

### Dependencies

Any `make` command depends on GNU make (`gmake`) and is expected to be run using it. This usually means using `make` on most GNU/Linux distros, or `gmake` on other non-GNU systems.

All `make all*` targets depend on a GNU-compatible C toolchain capable of generating x86-64 ELF objects. Usually `gcc/binutils` or `clang/llvm/lld` provided by any x86-64 UNIX like (including Linux) distribution will suffice.

Additionally, building an ISO with `make all` requires `xorriso`, and building a HDD/USB image with `make all-hdd` requires `sgdisk` (usually from `gdisk` or `gptfdisk` packages) and `mtools`.

### Makefile targets

Running `make all` will compile the kernel (from the `kernel/` directory) and then generate a bootable ISO image.

Running `make all-hdd` will compile the kernel and then generate a raw image suitable to be flashed onto a USB stick or hard drive/SSD.

Running `make run` will build the kernel and a bootable ISO (equivalent to make all) and then run it using `qemu` (if installed).

Running `make run-hdd` will build the kernel and a raw HDD image (equivalent to make all-hdd) and then run it using `qemu` (if installed).

The `run-uefi` and `run-hdd-uefi` targets are equivalent to their non `-uefi` counterparts except that they boot `qemu` using a UEFI-compatible firmware.
-->