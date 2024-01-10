# MunkOS

Operating System very early on in dev for learning purposes with simplicity in mind.

Currently, MunkOS only contains a kernel (with very minimal features), supports the x86_64 architecture, and boots up with the Limine Bootloader.

Stuff in the code marked with:

> // [DBG]

are removable debugprints, structures or variables.



# Building

It is recommended to use a Linux distro (WSL should work too) to build from source yourself. Building requires the following packages which you can install with your distros package manager (pacman, apt, ...):

[TODO] finish list, script for cross compiler toolchain
> sudo pacman -S gcc build-essential make xorriso gdisk mtools

1) 
2) 
```sh
```

### Third party software

- [Limine](https://www.github.com/limine-bootloader/limine)
- [flanterm](https://github.com/mintsuki/flanterm)
- [liballoc](https://github.com/blanham/liballoc)

# Features

Currently, work on the kernel is the biggest focus.

### Architecture stuff and basic initialization

- [x] Interrupts
- [x] PMM
- [x] VMM
- [x] Kernel-Heap
- [x] ACPI
- [ ] I/O APIC
- [ ] Timer
- [ ] PS2 driver
- [ ] Threads
- [ ] SMP
- [ ] Scheduler

### Long Term Goals

I want MunkOS to be a primarily dependency free OS, so replacing third party software I use for convinience during development with my own is something I intend to do at some point™.