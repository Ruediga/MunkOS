# MunkOS

MunkOS is an attempt to write a usable, educative, real-time operating system for x86-64 based computers (and planned support for aarch64) with simplicity in mind, that actually runs on real hardware successfully.

MunkOS core consists of a preemptively multi-tasked, smp-enabled kernel.

Currently, only the kernel (with yet quite minimal functionality), is being worked on, but you can look forward to userspace features soon.

# Building

Since I don't provide a custom cross-compiler toolchain script yet, compilation is only possible on x86_64 hosts.

You're going to have the least problems using Linux distro (WSL may work too) to build from source yourself. Building requires the following packages, which you can install with your distros package manager (pacman, apt, nix, apk, ...):

### Arch / Manjaro (pacman)

> sudo pacman -S git gcc make xorriso gdisk mtools curl python3 qemu-full

### Ubuntu / Debian / Mint (apt)

> sudo apt install git gcc build-essential make gdisk mtools curl python3 qemu-system-x86

In case the xorriso installation fails, try running `sudo add-apt-repository universe` and then `sudo apt install xorriso` again.

The following make-targets build the image / iso and run it with a default qemu config

```sh
all (default)
run-iso-bios
run-iso-uefi
run-img-bios
run-img-uefi
```

I recommend looking at the qemu flags in `Makefile` if qemu decides to not work.

Of course, a `clean` target exists too, and the `clean-full` target gets rid of installed dependencies.

If the kernel crashes or something doesn't work for you when testing on real hardware, and you don't have the same hardware as I do, your pc is broken and you should demand a refund from the manufacturer.

### Third party

- [Limine bootloader by mintsuki](https://www.github.com/limine-bootloader/limine)
- [flanterm by mintsuki](https://github.com/mintsuki/flanterm)
- [uACPI by CopyObject abuser](https://github.com/UltraOS/uACPI)

# Structure

Please don't question my build system btw

```
.
├── kernel
│   ├── deps            ->  dependencies
│   ├── include         ->  includes
│   └── src             ->  source files
├── res                 ->  config files and misc
└── tools               ->  third party and scripts
``````

# Features

Currently, I prioritize work on the kernel.

### Kernel

- [x] Arch specifics
- [x] PMM (buddy & bitmap)
- [x] Kheap (slab)
- [x] VMM
- [x] ACPI (+ uACPI)
- [x] SMP
- [x] Multitasking
- [x] Preemptive scheduler
- [x] Timer interface (WIP)
- [x] Event interface (WIP)

WIP:

- [ ] VFS
- [ ] device interface
- [x] partition devices
- [ ] FAT
- [ ] ext

### Hardware

- [x] PS2 keyboard
- [x] PCI(e)
- [x] NVME


### TODO

#### FIXME

nvme controller reporting ct=0 on some laptops

nvme driver should not buffer writes without actually writing

since i've had a bug with random deadlocks, I spammed a shitload of preempt-safe spinlocks everywhere -> fix this

#### Features

tlb shootdowns

pci multiple bridges

require x86-64 toolchain
& manage dependencies better

a proper device / driver interface

syscalls

vfs

### Long Term Goals

I want MunkOS to be a primarily dependency free OS, so replacing third party libraries I use for convinience during development with my own is something I want to do at some point™.

# Contributing

(Small) contributions are always welcome. Feel free to open a pull request.

### Contributors

[<img src="https://github.com/notbonzo-com.png" width="40">](https://github.com/notbonzo-com)

<html>
<blockquote>
    <p style="font-style: italic">"Just read the docs"</p>
    <footer>- A wise man</footer>
</blockquote>
<html>