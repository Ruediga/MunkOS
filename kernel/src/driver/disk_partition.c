#include "disk_partition.h"
#include "liballoc.h"
#include "memory.h"
#include "kprintf.h"
#include "macros.h"

// -1 on error, else bytes read
int partition_dev_read(struct device *dev, void *buf, size_t off, size_t count)
{
    struct partition_device *part = (struct partition_device *)dev;

    // if out of range
    if (DIV_ROUNDUP(off + count, part->dev.bsize) > part->dev.bcount) {
        return -1;
    }
    
    struct device *disk = (struct device *)part->dev.dev_specific;

    return disk->read(disk, buf, off + part->starting_lba * part->dev.bsize, count);
}

// -1 on error, else bytes written
int partition_dev_write(struct device *dev, void *buf, size_t off, size_t count)
{
    struct partition_device *part = (struct partition_device *)dev;

    // if out of range
    if (DIV_ROUNDUP(off + count, part->dev.bsize) > part->dev.bcount) {
        return -1;
    }
    
    struct device *disk = (struct device *)part->dev.dev_specific;

    return disk->write(disk, buf, off + part->starting_lba * part->dev.bsize, count);
}

// attempt to partition a (virtual) disk with either mbt or gpt. consequently, mount the partitions
// as devices with new r/w functions adjusted to the offset of each partition.
// r/w operations can then be issued from these devices, which (disk) filesystems (ext, fat, ...)
// have to use to read from disk. this function may be called from inside a device initializing
// driver interface so no further dependencies are created.
// this driver will name created devices the same as they were called before, but with the
// partition index appended!!! so for SATA drives, pass a drive called sda to get sda1 - ...,
// for NVME drives pass nvmeXnY to get nvmeXnYp1 - ... and so on
void partition_disk_device(struct device *vdisk)
{
    struct gpt_partition_table_header *lba1 = kmalloc(vdisk->bsize);
    // read lba1 to check for gpt
    uint64_t pos = 1 * vdisk->bsize;
    vdisk->read(vdisk, lba1, pos, vdisk->bsize);

    if (!memcmp(lba1->signature, "EFI PART", 8)) {
        // GPT
        pos = lba1->guid_pea_start_lba * vdisk->bsize;
        struct gpt_partition_table_entry *entry = kmalloc(lba1->pea_entry_size);

        for (size_t i = 0; i < lba1->partentries_count; i++) {
            vdisk->read(vdisk, entry, pos, lba1->pea_entry_size);
            pos += lba1->pea_entry_size;

            if (!entry->part_type_guid_low && !entry->part_type_guid_high) {
                continue;
            }

            struct partition_device *part = kmalloc(sizeof(struct partition_device));
            // gendptr points to underlying device (driver)
            device_init((struct device *)part, vdisk->bsize, entry->lba_end - entry->lba_start + 1,
                0, DEV_BLOCK, (gen_dptr)vdisk, "%sp%lu", vdisk->name, i + 1);
            part->dev.read = partition_dev_read;
            part->dev.write = partition_dev_write;
            part->starting_lba = entry->lba_start;

            kprintf("  - vfs::devfs::partition: found partition %s [start=%lu, length=%lu]\n",
                part->dev.name, part->starting_lba * part->dev.bsize, part->dev.bcount * part->dev.bsize);
        }

        kfree(entry);
    } else {
        // [FIXME] untested so probably doesn't work
        // https://en.wikipedia.org/wiki/Master_boot_record
        struct mbr_partition_format *mbr = (void *)lba1;
        vdisk->read(vdisk, mbr, 0, 512);
        if (mbr->signature_low != 0x55 || mbr->signature_high != 0xAA) {
            kprintf("  - vfs::devfs::partition: unsupported partition type\n");
            goto fail;
        }

        for (size_t i = 0; i < 4; i++) {
            // empty partition
            if (mbr->entries[i].part_type == 0x00) {
                continue;
            }

            struct partition_device *part = kmalloc(sizeof(struct partition_device));
            // gendptr points to underlying device (driver)
            device_init((struct device *)part, vdisk->bsize, mbr->entries[i].nsectors,
                0, DEV_BLOCK, (gen_dptr)vdisk, "%sp%lu", vdisk->name, i + 1);
            part->dev.read = partition_dev_read;
            part->dev.write = partition_dev_write;
            part->starting_lba = mbr->entries[i].start_lba;

            kprintf("  - vfs::devfs::partition: found partition %s [start=%lu, length=%lu]\n",
                part->dev.name, part->starting_lba * part->dev.bsize, part->dev.bcount * part->dev.bsize);
        }
    }

fail:
    kfree(lba1);
}