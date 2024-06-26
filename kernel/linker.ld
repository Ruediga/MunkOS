/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

/* We want the symbol _start to be our entry point */
ENTRY(kernel_entry)

/* Define the program headers we want so the bootloader gives us the right */
/* MMU permissions */
PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata  PT_LOAD    FLAGS((1 << 2)) ;            /* Read only */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ; /* Write + Read */
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2)) ; /* Dynamic PHDR for relocations */
}

SECTIONS
{
    /* top 2gib of addr space (limine spec) */
    . = 0xffffffff80000000;

    . = ALIGN(0x1000);
    text_start_addr = .;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);
    text_end_addr = .;

    /* Move to the next memory page for .rodata */
    . += CONSTANT(MAXPAGESIZE);

    . = ALIGN(0x1000);
    rodata_start_addr = .;

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);
    rodata_end_addr = .;

    /* Move to the next memory page for .data */
    . += CONSTANT(MAXPAGESIZE);

    . = ALIGN(0x1000);
    data_start_addr = .;

    .data : {
        *(.data .data.*)
    } :data

    /* Dynamic section for relocations, both in its own PHDR and inside data PHDR */
    .dynamic : {
        *(.dynamic)
    } :data :dynamic

    /* NOTE: .bss needs to be the last thing mapped to :data, otherwise lots of */
    /* unnecessary zeros will be written to the binary. */
    /* If you need, for example, .init_array and .fini_array, those should be placed */
    /* above this. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(0x1000);
    data_end_addr = .;


    /* Discard .note.* and .eh_frame since they may cause issues on some hosts. */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
