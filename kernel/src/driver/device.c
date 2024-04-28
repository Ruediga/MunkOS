#include "device.h"
#include "memory.h"
#include "kprintf.h"

int device_common_read(struct device *dev, void *buf, size_t off, size_t count)
{
    (void)dev;
    (void)buf;
    (void)off;
    (void)count;

    return 0;
}

int device_common_write(struct device *dev, void *buf, size_t off, size_t count)
{
    (void)dev;
    (void)buf;
    (void)off;
    (void)count;

    return 0;
}

// last arguments set device name
void device_init(struct device *dev, size_t bsize, size_t bcount, size_t flags, enum dev_type type, gen_dptr dev_specific, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvsnprintf(dev->name, DEVICE_NAME_LENGTH, fmt, args);
    va_end(args);

    dev->bcount = bcount;
    dev->bsize = bsize;
    dev->flags = flags;
    dev->type = type;
    dev->dev_specific = dev_specific;

    dev->read = device_common_read;
    dev->write = device_common_write;
}