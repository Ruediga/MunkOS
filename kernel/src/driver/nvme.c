#include "nvme.h"
#include "macros.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "interrupt.h"
#include "kprintf.h"

#ifdef MUNKOS_DEBUG_BUILD
static void nvme_cmdset_struct_verify_size()
{
    bool failed = failed;

    if (sizeof(nvme_scmd) != NVME_SQE_SIZE) failed = true;
    if (sizeof(nvme_ccmd) != NVME_CQE_SIZE) failed = true;

    nvme_scmd temp = {0};
    if (sizeof(temp.common_command_format_entry) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.compare) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.create_io_cq) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.create_io_sq) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.identify) != NVME_SQE_SIZE) failed = true;

    union nvme_identify_ds temp2 = {0};
    if (sizeof(temp2.ctrler) != nvme_identify_ds_SIZE) failed = true;
    if (sizeof(temp2.ns) != nvme_identify_ds_SIZE) failed = true;

    if (failed) kpanic(NULL, "NVME_INIT: structure size check failed:\n");
}
#endif // MUNKOS_DEBUG_BUILD

void nvme_debug_cqe_unsuccessful(nvme_ccmd cqe)
{
    kpanic(NULL, "nvme: cqe failed!\ncid = %u, phase = %u, status = %u, sqid = %u\n",
        (uint32_t)cqe.cid, (uint32_t)cqe.status & 1, (uint32_t)cqe.status >> 1, (uint32_t)cqe.sqid);
}

static void nvme_compatibility_check(struct nvme_controller *controller)
{
    // io cmd set
    if (controller->properties->cap & (1ul << 44)) kpanic(NULL, "NVME_INIT: controller doesn't support IO cmd set\n");
    // 4 KiB page size
    uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller->properties->cap) >> 48));
    uint64_t mpsmax = POW(2ul, 12 + (((0xFul << 52) & controller->properties->cap) >> 52));
    if (!(mpsmin <= PAGE_SIZE && mpsmax >= PAGE_SIZE)) {
        kpanic(NULL, "NVME_INIT: controller doesn't support host page size (mpsmin=%lX, mpsmax=%lX)\n", mpsmin, mpsmax);
    }
    // physically contig pages required
    if (controller->properties->cap & (1 << 16)) kpanic(NULL, "NVME_INIT: requires physically contiguous pages\n");
}

static void nvme_controller_reset(volatile struct nvme_controller_properties *properties)
{
    // turn controller off
    properties->cc = properties->cc & ~0b1;
    // wait until csts.rdy turns on
    while (properties->csts & NVME_STATUS_READY) __asm__ ("pause");
}

// since [at least] the admin queues require cc.mps alignment, manually align pages
static void nvme_init_queue(struct nvme_controller *controller, struct nvme_queue_ctx *queue, size_t id, size_t entries, size_t alignment)
{
    queue->entries = entries;
    queue->queue_id = id;

    // account for the possibility of buffer overflow during alignment
    queue->sq._metadata_pointer_unaligned = kcalloc(1, NVME_SQE_SIZE * entries + alignment);
    queue->cq._metadata_pointer_unaligned = kcalloc(1, NVME_CQE_SIZE * entries + alignment);
    queue->sq.data = (void *)ALIGN_UP((uintptr_t)queue->sq._metadata_pointer_unaligned, alignment);
    queue->cq.data = (void *)ALIGN_UP((uintptr_t)queue->cq._metadata_pointer_unaligned, alignment);

    // bar0 + 1000h + ((2y) * (4 << CAP.DSTRD)
    queue->sq.sqt = (void *)((uintptr_t)controller->properties + PAGE_SIZE + ((2 * id) * (4 << POW(2, 2 + NVME_PROPERTIES_DSTRD(controller->properties)))));
    queue->cq.cqh = (void *)((uintptr_t)controller->properties + PAGE_SIZE + ((2 * id + 1) * (4 << POW(2, 2 + NVME_PROPERTIES_DSTRD(controller->properties)))));

    queue->sq.head = queue->sq.tail = queue->cq.head = queue->cq.phase = 0;
}

// returns success (0 = error), generic queue manipulation to append new sq entry
bool nvme_queue_submit_single_cmd(struct nvme_queue_ctx *queue, nvme_scmd *cmd)
{
    // if sq if full, do not submit entry, and abort
    if (queue->sq.free_entries == queue->entries - 1) return false;

    // update cid (upon 16 bit overflow, conviniently resets to 0)
    cmd->common_command_format_entry.cid = queue->sq.cid_counter++;

    // append new cmd
    queue->sq.data[queue->sq.tail] = *(cmd);
    queue->sq.tail = queue->sq.tail % queue->entries;

    // update doorbell
    *queue->sq.sqt = queue->sq.tail;

    queue->sq.free_entries--;

    return true;
}

// instead of waiting for an interrupt, manually poll from the queue
nvme_ccmd nvme_queue_poll_single_cqe(struct nvme_queue_ctx *queue)
{
    uint16_t status;

    for (;;) {
        status = queue->cq.data[queue->cq.head].status;
        if ((status & 1) == queue->cq.phase) break;
        __asm__ ("pause");
    }

    nvme_ccmd out = queue->cq.data[queue->cq.head];

    queue->cq.head++;
    if (queue->cq.head == queue->entries) {
        queue->cq.head = 0;
        // invert phase bit upon wrapping around
        queue->cq.phase = !queue->cq.phase;
    }

    *(queue->cq.cqh) = queue->cq.head;

    queue->sq.free_entries++;

    return out;
}

// return NULL if status=FAILURE, else return pointer to allocated identify structure
union nvme_identify_ds *nvme_issue_cmd_identify(struct nvme_controller *controller, uint8_t cns, uint16_t cntid)
{
    union nvme_identify_ds *identifier = kmalloc(nvme_identify_ds_SIZE);
    nvme_scmd cmd = {
        .identify.opc = 0,
        .identify.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        .identify.prp1 = (uintptr_t)identifier - hhdm->offset,
        .identify.cns = cns,
        .identify.cntid = cntid,
        .identify.cns_specific_id = 0,  // not required for supported cns'es
        .identify.csi = 0,  // nvm cmd set
        .identify.uuid = 0
    };
    // use prp2 if pointer is not page aligned
    if ((uintptr_t)identifier % PAGE_SIZE) {
        cmd.identify.prp2 = ALIGN_UP((uintptr_t)identifier - hhdm->offset, PAGE_SIZE);
    }

    if (!nvme_queue_submit_single_cmd(&controller->aq, &cmd)) kpanic(NULL, "NVME_INIT: failed to identify nvme controller\n");
    nvme_ccmd cqe = nvme_queue_poll_single_cqe(&controller->aq);

    if (!NVME_CQE_SUCCESSFUL(cqe)) return NULL;

    return identifier;
}

// each controller (even if on the same drive) appears as seperate pci(e) device.
// controller types are admin, io and discovery
void init_nvme_controller(pci_device *dev)
{
#ifdef MUNKOS_DEBUG_BUILD
    nvme_cmdset_struct_verify_size();
#endif // MUNKOS_DEBUG_BUILD

    struct nvme_controller controller = {0};

    // interrupts enable, bus-mastering DMA, memory space access
    pci_set_command_reg(dev, PCI_CMD_REG_FLAG_BUS_MASTER | PCI_CMD_REG_FLAG_MMIO);
    // map bar0
    struct pci_base_addr_reg_ctx nvme_pci_bar0 = {0};
    pci_read_bar(dev, &nvme_pci_bar0, 0);
    kprintf("%lX, len=%lu\n", nvme_pci_bar0.base, nvme_pci_bar0.size);

    if (!nvme_pci_bar0.is_mmio_bar) kpanic(NULL, "NVME_INIT: pci bar0 is not mmio mapped\n");
    for (uintptr_t ptr = ALIGN_DOWN((uintptr_t)nvme_pci_bar0.base, PAGE_SIZE);
        ptr < ALIGN_UP((uintptr_t)nvme_pci_bar0.base + nvme_pci_bar0.size, PAGE_SIZE); ptr += PAGE_SIZE) {
        vmm_unmap_single_page(&kernel_pmc, ptr + hhdm->offset, false); // do not free page!
        vmm_map_single_page(&kernel_pmc, ptr + hhdm->offset, ptr,
            PTE_BIT_DISABLE_CACHING | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    }

    controller.properties = (volatile struct nvme_controller_properties *)((uintptr_t)nvme_pci_bar0.base + hhdm->offset);

    if (!controller.properties->version) kpanic(NULL, "NVME_INIT: invalid controller version\n");

    // compatibility checks
    nvme_compatibility_check(&controller);

    // reset controller
    nvme_controller_reset(controller.properties);

    // set controller config: 16 byte (2⁴) cq entries, 64 byte (2⁶) sq entries, RR AMS (supported by all ctrlers,
    // for other algos check cap.ams), 4KiB pages (2^12+n), support NVMe cmd set (admin and io)
    controller.properties->cc = (0b100 << 20) | (0b110 << 16) | (0 << 11) | (0 << 7) | (0 << 4);

    // set aqa and asq/acq
    // attributes: asqs and acqs max size: 4 KiB (0 based!)
    uint16_t entries = NVME_PROPERTIES_MQES(controller.properties) + 1;
    kprintf("%u\n", (uint32_t)entries);
    if (entries >= 0x1000) entries = 0x1000;
    controller.properties->aqa = (entries - 1) | ((entries - 1) << 16);

    // base addresses seem to require cc.mps alignment, 4KiB in this case
    if (POW(2, 12 + ((controller.properties->cc & (0xF << 7)) >> 7)) != PAGE_SIZE) kpanic(NULL, "NVME_INIT: cc.mps is configured incorrectly\n");
    nvme_init_queue(&controller, &controller.aq, 0, entries, PAGE_SIZE);
    controller.properties->asq = (uint64_t)controller.aq.sq.data;
    controller.properties->acq = (uint64_t)controller.aq.cq.data;

    // reenable controller
    controller.properties->cc = controller.properties->cc & 0b1;

    // register msi-x interrupt [TODO]

    // identify ctrler (cntid not required) and 
    controller.ctrler_identify = nvme_issue_cmd_identify(&controller, NVME_CNS_CONTROLLER, 0);
    if (!controller.ctrler_identify) kpanic(NULL, "NVME_INIT: failed to identify nvme controller\n");

    kprintf("%s\n", (char *)&controller.ctrler_identify->ctrler.mn[0]);
    // is io ctrler?
    if (controller.ctrler_identify->ctrler.cntrltype != NVME_CTRLER_TYPE_IO) {
        kprintf("found non-io controller: type=%lX not supported.\n", (uint64_t)controller.ctrler_identify->ctrler.cntrltype);
        // clear up controller structures?
        return;
    }
    controller.cntlid = controller.ctrler_identify->ctrler.cntlid;

    // record mdts
    if (controller.ctrler_identify->ctrler.mdts) {
        uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller.properties->cap) >> 48));
        controller.mdts = POW(2, controller.ctrler_identify->ctrler.mdts) * mpsmin;
    } else {
        controller.mdts = ~0ul;
    }

    // reset software progress marker, if supported


}