#include "uacpi_stdlib.h"

#include "kevent.h"
#include "locking.h"
#include "memory.h"
#include "macros.h"
#include "kprintf.h"
#include "frame_alloc.h"
#include "kheap.h"
#include "io.h"
#include "mmu.h"
#include "scheduler.h"
#include "time.h"
#include "cpu.h"
#include "compiler.h"
#include "uacpi/platform/types.h"
#include "uacpi/status.h"

// kernel api
// ============================================================================
#include <stdint.h>
#include <uacpi/types.h>
#include <uacpi/platform/arch_helpers.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raw IO API, this is only used for accessing verified data from
 * "safe" code (aka not indirectly invoked by the AML interpreter),
 * e.g. programming FADT & FACS registers.
 *
 * NOTE:
 * 'byte_width' is ALWAYS one of 1, 2, 4, 8. You are NOT allowed to implement
 * this in terms of memcpy, as hardware expects accesses to be of the EXACT
 * width.
 * -------------------------------------------------------------------------
 */
uacpi_status uacpi_kernel_raw_memory_read(
    uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 *out_value
)
{
		void* virt = (void *)address + hhdm->offset;

		switch (byte_width)
		{
			case 1: *out_value = *( uint8_t*)virt; break;
			case 2: *out_value = *(uint16_t*)virt; break;
			case 4: *out_value = *(uint32_t*)virt; break;
			case 8: *out_value = *(uint64_t*)virt; break;
			default: return UACPI_STATUS_INVALID_ARGUMENT;
		}
		return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_raw_memory_write(
    uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 in_value
)
{
		void* virt = (void *)address + hhdm->offset;

		switch (byte_width)
		{
			case 1: *( uint8_t*)virt = in_value; break;
			case 2: *(uint16_t*)virt = in_value; break;
			case 4: *(uint32_t*)virt = in_value; break;
			case 8: *(uint64_t*)virt = in_value; break;
			default: return UACPI_STATUS_INVALID_ARGUMENT;
		}
		return UACPI_STATUS_OK;
	}

/*
 * NOTE:
 * 'byte_width' is ALWAYS one of 1, 2, 4. You are NOT allowed to break e.g. a
 * 4-byte access into four 1-byte accesses. Hardware ALWAYS expects accesses to
 * be of the exact width.
 */
uacpi_status uacpi_kernel_raw_io_read(
    uacpi_io_addr address, uacpi_u8 byte_width, uacpi_u64 *out_value
)
{
		if (byte_width > 8)
			return UACPI_STATUS_INVALID_ARGUMENT;
		uacpi_status status = UACPI_STATUS_OK;
		if (byte_width == 1)
			*out_value = inb(address);
		if (byte_width == 2)
			*out_value = inw(address);
		if (byte_width == 4)
			*out_value = inl(address);
		if (byte_width == 8)
			status = UACPI_STATUS_INVALID_ARGUMENT;
		return status;
	}
uacpi_status uacpi_kernel_raw_io_write(
    uacpi_io_addr address, uacpi_u8 byte_width, uacpi_u64 in_value
)
{
		if (byte_width > 8)
			return UACPI_STATUS_INVALID_ARGUMENT;
		uacpi_status status = UACPI_STATUS_OK;
		if (byte_width == 1)
			outb(address, in_value);
		if (byte_width == 2)
			outw(address, in_value);
		if (byte_width == 4)
			outl(address, in_value);
		if (byte_width == 8)
			status = UACPI_STATUS_INVALID_ARGUMENT;
		return status;
	}
// -------------------------------------------------------------------------

#include "pci.h"

void pciWriteByteRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t data)
    {
        uint32_t address;
        uint32_t lbus = (uint32_t)bus;
        uint32_t lslot = (uint32_t)slot;
        uint32_t lfunc = (uint32_t)func;

        address = (uint32_t)((lbus << 16) | (lslot << 11) |
            (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

        outl(0xCF8, address);
        outb(0xCFC, data);
    }
    void pciWriteWordRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t data)
    {
        uint32_t address;
        uint32_t lbus = (uint32_t)bus;
        uint32_t lslot = (uint32_t)slot;
        uint32_t lfunc = (uint32_t)func;

        address = (uint32_t)((lbus << 16) | (lslot << 11) |
            (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

        outl(0xCF8, address);
        outw(0xCFC, data);
    }
    void pciWriteDwordRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data)
    {
        uint32_t address;
        uint32_t lbus = (uint32_t)bus;
        uint32_t lslot = (uint32_t)slot;
        uint32_t lfunc = (uint32_t)func;

        address = (uint32_t)((lbus << 16) | (lslot << 11) |
            (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

        outl(0xCF8, address);
        outl(0xCFC, data);
    }
	 uint8_t pciReadByteRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
        {
            uint32_t address;
            uint32_t lbus = (uint32_t)bus;
            uint32_t lslot = (uint32_t)slot;
            uint32_t lfunc = (uint32_t)func;

            address = (uint32_t)((lbus << 16) | (lslot << 11) |
                (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

            outl(0xCF8, address);

            uint8_t ret = (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFFFF);
            return ret;
        }
        uint16_t pciReadWordRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
        {
            uint32_t address;
            uint32_t lbus = (uint32_t)bus;
            uint32_t lslot = (uint32_t)slot;
            uint32_t lfunc = (uint32_t)func;

            address = (uint32_t)((lbus << 16) | (lslot << 11) |
                (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

            outl(0xCF8, address);

            uint16_t ret = (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
            return ret;
        }
        uint32_t pciReadDwordRegister(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
        {
            uint32_t address;
            uint32_t lbus = (uint32_t)bus;
            uint32_t lslot = (uint32_t)slot;
            uint32_t lfunc = (uint32_t)func;

            address = (uint32_t)((lbus << 16) | (lslot << 11) |
                (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

            outl(0xCF8, address);

            return ((inl(0xCFC) >> ((offset & 2) * 8)));
        }

/*
 * NOTE:
 * 'byte_width' is ALWAYS one of 1, 2, 4. Since PCI registers are 32 bits wide
 * this must be able to handle e.g. a 1-byte access by reading at the nearest
 * 4-byte aligned offset below, then masking the value to select the target
 * byte.
 */
uacpi_status uacpi_kernel_pci_read(
    uacpi_pci_address *address, uacpi_size offset,
    uacpi_u8 byte_width, uacpi_u64 *value
)
{
    if (address->segment)
        return UACPI_STATUS_UNIMPLEMENTED;
    switch (byte_width)
    {
        case 1: *value = pciReadByteRegister(address->bus, address->device, address->function, offset); break;
        case 2: *value = pciReadWordRegister(address->bus, address->device, address->function, offset); break;
        case 4: *value = pciReadDwordRegister(address->bus, address->device, address->function, offset); break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_write(
    uacpi_pci_address *address, uacpi_size offset,
    uacpi_u8 byte_width, uacpi_u64 value
)
{
		if (address->segment)
			return UACPI_STATUS_UNIMPLEMENTED;
		switch (byte_width)
		{
			case 1: pciWriteByteRegister(address->bus, address->device, address->function, offset, value & 0xff); break;
			case 2: pciWriteWordRegister(address->bus, address->device, address->function, offset, value & 0xffff); break;
			case 4: pciWriteDwordRegister(address->bus, address->device, address->function, offset, value & 0xffffffff); break;
			default: return UACPI_STATUS_INVALID_ARGUMENT;
		}
		return UACPI_STATUS_OK;
	}

/*
 * Map a SystemIO address at [base, base + len) and return a kernel-implemented
 * handle that can be used for reading and writing the IO range.
 */
struct io_range
	{
		uacpi_io_addr base;
		uacpi_size len;
	};
uacpi_status uacpi_kernel_io_map(
    uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle
)
{
		if (base > 0xffff)
			return UACPI_STATUS_INVALID_ARGUMENT;
		struct io_range* rng = kcalloc(1, sizeof(struct io_range));
        rng->base = base;
        rng->len = len;
		*out_handle = (uacpi_handle)rng;
		return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle handle)
{
    kfree(handle);
}

/*
 * Read/Write the IO range mapped via uacpi_kernel_io_map
 * at a 0-based 'offset' within the range.
 *
 * NOTE:
 * 'byte_width' is ALWAYS one of 1, 2, 4. You are NOT allowed to break e.g. a
 * 4-byte access into four 1-byte accesses. Hardware ALWAYS expects accesses to
 * be of the exact width.
 */
uacpi_status uacpi_kernel_io_read(
    uacpi_handle hnd, uacpi_size offset,
    uacpi_u8 byte_width, uacpi_u64 *value
)
{
		struct io_range* rng = (struct io_range*)hnd;
		if (offset >= rng->len)
			return UACPI_STATUS_INVALID_ARGUMENT;
		return uacpi_kernel_raw_io_read(rng->base + offset, byte_width, value);
}
uacpi_status uacpi_kernel_io_write(
    uacpi_handle hnd, uacpi_size offset,
    uacpi_u8 byte_width, uacpi_u64 value
)
{
		struct io_range* rng = (struct io_range*)hnd;
		if (offset >= rng->len)
			return UACPI_STATUS_INVALID_ARGUMENT;
		return uacpi_kernel_raw_io_write(rng->base + offset, byte_width, value);
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    // map to hhdm
    uintptr_t start = ALIGN_DOWN(addr, PAGE_SIZE);
    len = ALIGN_UP(len, PAGE_SIZE);
    // map as write maybe?
    mmu_map_range_linear(&kernel_pmc, start + hhdm->offset,
        start, len, MAF_WRITE, MCT_WRITE_BACK);
    return (void *)((uintptr_t)addr + hhdm->offset);
}
void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    (void)addr;
    (void)len;
}

/*
 * Allocate a block of memory of 'size' bytes.
 * The contents of the allocated memory are unspecified.
 */
void *uacpi_kernel_alloc(uacpi_size size)
{
    return kcalloc(1, size);
}

/*
 * Allocate a block of memory of 'count' * 'size' bytes.
 * The returned memory block is expected to be zero-filled.
 */
void *uacpi_kernel_calloc(uacpi_size count, uacpi_size size)
{
    return kcalloc(count, size);
}

/*
 * Free a previously allocated memory block.
 *
 * 'mem' might be a NULL pointer. In this case, the call is assumed to be a
 * no-op.
 *
 * An optionally enabled 'size_hint' parameter contains the size of the original
 * allocation. Note that in some scenarios this incurs additional cost to
 * calculate the object size.
 */
#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void *mem)
{
    kfree(mem);
}
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint);
#endif

enum uacpi_log_level {
    /*
     * Super verbose logging, every op & uop being processed is logged.
     * Mostly useful for tracking down hangs/lockups.
     */
    UACPI_LOG_DEBUG = 4,

    /*
     * A little verbose, every operation region access is traced with a bit of
     * extra information on top.
     */
    UACPI_LOG_TRACE = 3,

    /*
     * Only logs the bare minimum information about state changes and/or
     * initialization progress.
     */
    UACPI_LOG_INFO  = 2,

    /*
     * Logs recoverable errors and/or non-important aborts.
     */
    UACPI_LOG_WARN  = 1,

    /*
     * Logs only critical errors that might affect the ability to initialize or
     * prevent stable runtime.
     */
    UACPI_LOG_ERROR = 0,
};

void uacpi_kernel_vlog(enum uacpi_log_level level, const char* format, uacpi_va_list list)
{
    const char* prefix = "UNKNOWN";
    switch (level)
    {
    case UACPI_LOG_TRACE:
        prefix = "TRACE";
        break;
    case UACPI_LOG_INFO:
        prefix = "INFO";
        break;
    case UACPI_LOG_WARN:
        prefix = "WARN";
        break;
    case UACPI_LOG_ERROR:
        prefix = "ERROR";
        break;
    default:
        break;
    }
    kprintf("  - uacpi: %s: ", prefix);
    kvprintf(format, list);
}
void uacpi_kernel_log(enum uacpi_log_level level, const char* format, ...)
{
    va_list list;
    va_start(list, format);
    uacpi_kernel_vlog(level, format, list);
    va_end(list);
}

/*
 * Returns the number of 100 nanosecond ticks elapsed since boot,
 * strictly monotonic.
 */
uacpi_u64 uacpi_kernel_get_ticks(void)
{
    // read from rtc?
    // 1000 ms ticks
    return system_ticks * 10000;
}

/*
 * Spin for N microseconds.
 */
void uacpi_kernel_stall(uacpi_u8 usec)
{
    size_t end = system_ticks + usec * 1000;
    while (system_ticks < end) {
        arch_spin_hint();
    }
}

/*
 * Sleep for N milliseconds.
 */
void uacpi_kernel_sleep(uacpi_u64 msec)
{
    // should yield here
    size_t end = system_ticks + msec;
    while (system_ticks < end) {
        arch_spin_hint();
    }
}

/*
 * Create/free an opaque non-recursive kernel mutex object.
 */
struct mutex {
    k_spinlock_t spinlock;
};
uacpi_handle uacpi_kernel_create_mutex(void)
{
    struct mutex *lock = kcalloc(1, sizeof(struct mutex));

    return lock;
}
void uacpi_kernel_free_mutex(uacpi_handle hnd)
{
    kfree(hnd);
}

/*
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void)
{
    kevent_t *ev = kcalloc(1, sizeof(kevent_t)); 
    return ev;
}
void uacpi_kernel_free_event(uacpi_handle hnd)
{
    kfree(hnd);
}

/*
 * Try to acquire the mutex with a millisecond timeout.
 * A timeout value of 0xFFFF implies infinite wait.
 */
uacpi_bool uacpi_kernel_acquire_mutex(uacpi_handle hnd, uacpi_u16 t)
{
    struct mutex *mutex = hnd;
    spin_lock_timeout(&mutex->spinlock, t);
    return UACPI_TRUE;
}
void uacpi_kernel_release_mutex(uacpi_handle hnd)
{
    struct mutex *mutex = hnd;
    spin_unlock_global(&mutex->spinlock);
}

/*
 * Try to wait for an event (counter > 0) with a millisecond timeout.
 * A timeout value of 0xFFFF implies infinite wait.
 *
 * The internal counter is decremented by 1 if wait was successful.
 *
 * A successful wait is indicated by returning UACPI_TRUE.
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle hnd, uacpi_u16 timeout)
{
    (void)timeout;

    kevent_t *ev = (kevent_t *)hnd;
    kevent_t *events[] = {ev};
    int ret = kevents_poll(events, 1);
    if (ret != 1)
        return false;
    return true;
}

/*
 * Signal the event object by incrementing its internal counter by 1.
 *
 * This function may be used in interrupt contexts.
 */
void uacpi_kernel_signal_event(uacpi_handle)
{
    kpanic(0, NULL, "you didnt implement this");
}

/*
 * Reset the event counter to 0.
 */
void uacpi_kernel_reset_event(uacpi_handle)
{
    kpanic(0, NULL, "you didnt implement this");
}

/*
 * Handle a firmware request.
 *
 * Currently either a Breakpoint or Fatal operators.
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* req)
{
    if (req->type == UACPI_FIRMWARE_REQUEST_TYPE_FATAL) {
        kpanic(0, NULL, "your firmware wants you dead :(");
        unreachable();
    }
    return true;
}

/*
 * Install an interrupt handler at 'irq', 'ctx' is passed to the provided
 * handler for every invocation.
 *
 * 'out_irq_handle' is set to a kernel-implemented value that can be used to
 * refer to this handler from other API.
 */
uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle
)
{
    (void)irq;
    (void)ctx;
    (void)out_irq_handle;
    // [TODO] mplement some proper way of installing int handlers that you can
    // pass data to since the current implementation is shit
    kprintf("WARNING: uACPI stdlib: no interrupts getting registered since I didnt implement this\n");
    return UACPI_STATUS_OK;
}

/*
 * Uninstall an interrupt handler. 'irq_handle' is the value returned via
 * 'out_irq_handle' during installation.
 */
uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler, uacpi_handle irq_handle
)
{
    (void)irq_handle;
    return UACPI_STATUS_OK;
}

/*
 * Create/free a kernel spinlock object.
 *
 * Unlike other types of locks, spinlocks may be used in interrupt contexts.
 */
uacpi_handle uacpi_kernel_create_spinlock(void)
{
    k_spinlock_t *lock = kcalloc(1, sizeof(k_spinlock_t));

    return lock;
}
void uacpi_kernel_free_spinlock(uacpi_handle hnd)
{
    kfree(hnd);
}

/*
 * Lock/unlock helpers for spinlocks.
 *
 * These are expected to disable interrupts, returning the previous state of cpu
 * flags, that can be used to possibly re-enable interrupts if they were enabled
 * before.
 *
 * Note that lock is infalliable.
 */
uacpi_cpu_flags uacpi_kernel_spinlock_lock(uacpi_handle hnd)
{
    int_status_t s = preempt_fetch_disable();
    k_spinlock_t *lock = hnd;
    spin_lock_global(lock);
    return (uacpi_cpu_flags)s;
}
void uacpi_kernel_spinlock_unlock(uacpi_handle hnd, uacpi_cpu_flags flgs)
{
    k_spinlock_t *lock = hnd;
    spin_unlock_global(lock);
    preempt_restore((int_status_t)flgs);
}

typedef enum uacpi_work_type {
    /*
     * Schedule a GPE handler method for execution.
     * This should be scheduled to run on CPU0 to avoid potential SMI-related
     * firmware bugs.
     */
    UACPI_WORK_GPE_EXECUTION,

    /*
     * Schedule a Notify(device) firmware request for execution.
     * This can run on any CPU.
     */
    UACPI_WORK_NOTIFICATION,
} uacpi_work_type;

typedef void (*uacpi_work_handler)(uacpi_handle);

/*
 * Schedules deferred work for execution.
 * Might be invoked from an interrupt context.
 */
uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type, uacpi_work_handler, uacpi_handle ctx
)
{
    (void)ctx;
    return UACPI_STATUS_OK;
}

/*
 * Blocks until all scheduled work is complete and the work queue becomes empty.
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
    return UACPI_STATUS_OK;
}

/*
 * Returns a unique identifier of the currently executing thread.
 *
 * The returned thread id cannot be UACPI_THREAD_ID_NONE.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
    // [FIXME]
    int_status_t is = preempt_fetch_disable();
    int tid = scheduler_curr_task()->tid;
    preempt_restore(is);

    (void)tid;
    return (void *)(uint64_t)0x88;
}

#ifdef __cplusplus
}
#endif