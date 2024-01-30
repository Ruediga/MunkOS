# MunkOS

Future Operating System very early on in development for learning purposes with simplicity in mind.

Currently, MunkOS only consists of a kernel (with very minimal functionality), supports the x86_64 architecture, and boots with the Limine Bootloader.

# Building

You're going to have the least problems using Linux distro like Arch (or derivatives) (WSL may work too) to build from source yourself. Building requires the following packages, which you can install with your distros package manager (pacman, apt, apk, ...):

> sudo pacman -S gcc build-essential make xorriso gdisk mtools

The following make-targets build the image / iso and run qemu

```sh
run-iso-bios
run-iso-uefi
run-img-bios
run-img-uefi
```

If the kernel crashes or something doesn't work for you when testing on real hardware, and you don't have the same cpu as I do (family=6, model=141, stepping=1), your hardware is broken and you should demand a refund.

### Third party

- [Limine](https://www.github.com/limine-bootloader/limine)
- [flanterm](https://github.com/mintsuki/flanterm)
- [liballoc](https://github.com/blanham/liballoc)

# Features

Currently, I prioritize work on the kernel.

### Kernel

- [x] PMM
- [x] VMM (add self balancing interval tree)
- [x] Kernel-Heap
- [x] Interrupts
- [x] ACPI
- [x] I/O APIC
- [x] LAPIC
- [x] Timer (PIT & LAPIC)
- [x] PS2 driver
- [x] SMP
- [x] Threads & Processes
- [x] Scheduler
- [ ] NVME driver
- [ ] FS
- [ ] Userland


### TODO

cleaner mm abstraction, especially for scheduler

syscalls

file system

do some stuff with TSC

### Long Term Goals

I want MunkOS to be a primarily dependency free OS, so replacing third party libraries I use for convinience during development with my own is something I intend to do at some point™.

<blockquote style="border-left: 0.5em solid rgb(30,144,255);
    padding: 1em; font-size: 1.1em;">
    <p style="font-style: italic">"The SDM knows the answer to everything."</p>
    <footer style="color: rgb(30,144,255); text-align: right;">- A wise man</footer>
</blockquote>