# MunkOS

Operating System very early on in development for learning purposes with simplicity in mind.

Currently, MunkOS only contains a kernel (with very minimal features), supports the x86_64 architecture, and boots with the Limine Bootloader.

# Building

[TODO] finish list, script for cross compiler toolchain?

You're going to have the least problems using Linux distro like Arch (WSL may work too) to build from source yourself. Building requires the following packages, which you can install with your distros package manager (pacman, apt, apk, ...):

> sudo pacman -S gcc build-essential make xorriso gdisk mtools

The following MAKE-targets build the image / iso and run qemu

```sh
run-iso-bios
run-iso-uefi
run-img-bios
run-img-uefi
```

If it crashes or something doesn't work for you when testing on real hardware, and you don't have the same cpu as I do (cpuid leaf 0x1: family=6, model=141, stepping=1), your hardware is broken and you should demand a refund.

### Third party software

- [Limine](https://www.github.com/limine-bootloader/limine)
- [flanterm](https://github.com/mintsuki/flanterm)
- [liballoc](https://github.com/blanham/liballoc)

# Features

Currently, I prioritize work on the kernel.

### Architecture stuff and basic initialization

- [x] PMM
- [x] VMM
- [x] Kernel-Heap
- [x] Interrupts
- [x] ACPI
- [x] I/O APIC
- [x] LAPIC
- [ ] Timer
- [x] PS2 driver
- [ ] Threads
- [ ] SMP
- [ ] Scheduler

### Long Term Goals

I want MunkOS to be a primarily dependency free OS, so replacing third party software I use for convinience during development with my own is something I intend to do at some point™.