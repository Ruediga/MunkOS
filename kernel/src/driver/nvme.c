#include "nvme.h"
#include "macros.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "interrupt.h"
#include "kprintf.h"
#include "kheap.h"
#include "math.h"

VECTOR_TMPL_TYPE_NON_NATIVE(nvme_ns_ctx)

#ifdef MUNKOS_DEBUG_BUILD
static void nvme_cmdset_struct_verify_size()
{
    bool failed = failed;

    if (sizeof(nvme_scmd_t) != NVME_SQE_SIZE) failed = true;
    if (sizeof(nvme_ccmd_t) != NVME_CQE_SIZE) failed = true;

    nvme_scmd_t temp = {0};
    if (sizeof(temp.common_command_format_entry) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.compare) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.create_io_cq) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.create_io_sq) != NVME_SQE_SIZE) failed = true;
    if (sizeof(temp.identify) != NVME_SQE_SIZE) failed = true;

    union nvme_identify_ds temp2 = {0};
    if (sizeof(temp2.ctrler) != NVME_IDENTIFY_DS_SIZE) failed = true;
    if (sizeof(temp2.active_nsid_list) != NVME_IDENTIFY_DS_SIZE) failed = true;
    if (sizeof(temp2.namespace) != NVME_IDENTIFY_DS_SIZE) failed = true;

    if (failed) kpanic(NULL, "NVME_INIT: structure size check failed:\n");
}
#endif // MUNKOS_DEBUG_BUILD

void nvme_debug_cqe_unsuccessful(nvme_ccmd_t cqe)
{
    kprintf("nvme: cqe failed!\ncid = %u, phase = %u, status = %u [sct=%X, sc=%X], sqid = %u\n",
        (uint32_t)cqe.cid, (uint32_t)cqe.status & 1, (uint32_t)cqe.status >> 1,
        (((uint32_t)cqe.status >> 1) >> 8) & 0b111, ((uint32_t)cqe.status >> 1) & 0xFF, (uint32_t)cqe.sqid);
}

static void nvme_compatibility_check(struct nvme_controller *controller)
{
    // 4 KiB page size
    uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller->properties->cap) >> 48));
    uint64_t mpsmax = POW(2ul, 12 + (((0xFul << 52) & controller->properties->cap) >> 52));
    if (!(mpsmin <= PAGE_SIZE && mpsmax >= PAGE_SIZE)) {
        kpanic(NULL, "NVME_INIT: controller doesn't support host page size (mpsmin=%lX, mpsmax=%lX)\n", mpsmin, mpsmax);
    }
    // physically contig pages required [TODO]
    //if (controller->properties->cap & (1 << 16)) kpanic(NULL, "NVME_INIT: requires physically contiguous pages\n");
}

static void nvme_controller_reset(volatile struct nvme_controller_properties *properties)
{
    // turn controller off
    properties->cc = properties->cc & ~0b1;
    // wait until csts.rdy turns on
    while (properties->csts & NVME_STATUS_READY) __asm__ ("pause");
}

// since [at least] the admin queues require cc.mps alignment
static void nvme_init_queue(struct nvme_controller *controller, struct nvme_queue_ctx *queue, size_t id, size_t entries)
{
    queue->entries = entries;
    queue->queue_id = id;

    // [FIXME] don't let this suffer from my awful heap implementation anymore
    size_t pages_sq = DIV_ROUNDUP(NVME_SQE_SIZE * entries, PAGE_SIZE);
    size_t pages_cq = DIV_ROUNDUP(NVME_CQE_SIZE * entries, PAGE_SIZE);
    
    void *sq = pmm_claim_contiguous_pages(pages_sq);
    void *cq = pmm_claim_contiguous_pages(pages_cq);

    for (size_t page = 0; page < pages_sq; page++) {
        vmm_map_single_page(&kernel_pmc, hhdm->offset + (uintptr_t)sq + page * PAGE_SIZE,
            (uintptr_t)sq + page * PAGE_SIZE, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_READ_WRITE);
    }
    for (size_t page = 0; page < pages_cq; page++) {
        vmm_map_single_page(&kernel_pmc, hhdm->offset + (uintptr_t)cq + page * PAGE_SIZE,
            (uintptr_t)cq + page * PAGE_SIZE, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_READ_WRITE);
    }

    queue->sq.data = (void *)((uintptr_t)sq + hhdm->offset);
    queue->cq.data = (void *)((uintptr_t)cq + hhdm->offset);
    
    memset((void *)queue->sq.data, 0, NVME_SQE_SIZE * entries);
    memset((void *)queue->cq.data, 0, NVME_CQE_SIZE * entries);

    // bar0 + 1000h + ((2y [+ 1]) * (4 << CAP.DSTRD)
    queue->sq.sqt = (volatile void *)((uintptr_t)controller->properties + 0x1000 + ((2 * id) * (4 << NVME_PROPERTIES_DSTRD(controller->properties))));
    queue->cq.cqh = (volatile void *)((uintptr_t)controller->properties + 0x1000 + ((2 * id + 1) * (4 << NVME_PROPERTIES_DSTRD(controller->properties))));

    queue->sq.head = queue->sq.cid_counter = queue->sq.tail = queue->cq.head = 0;

    // starts off as 0 so on first received command it's 1
    queue->cq.phase = 1;

    queue->sq.free_entries = entries - 1;
}

// returns success (0 = error), generic queue manipulation to append new sq entry
bool nvme_queue_submit_single_cmd(struct nvme_queue_ctx *queue, nvme_scmd_t *cmd)
{
    // if sq if full, do not submit entry, and abort
    if (queue->sq.free_entries == 0) return false;

    // update cid (upon word overflow, conviniently resets to 0)
    cmd->common_command_format_entry.cid = queue->sq.cid_counter++;

    // append new cmd
    queue->sq.data[queue->sq.tail++] = *(cmd);
    queue->sq.tail = queue->sq.tail % queue->entries;

    // update doorbell
    *queue->sq.sqt = queue->sq.tail;

    queue->sq.free_entries--;

    return true;
}

// instead of waiting for an interrupt, manually poll from the queue
nvme_ccmd_t nvme_queue_poll_single_cqe(struct nvme_queue_ctx *queue)
{
    uint16_t status;

    size_t deadlock_count = 0;
    for (; deadlock_count <= 1000000; deadlock_count++) {
        status = queue->cq.data[queue->cq.head].status;
        if ((status & 1) == queue->cq.phase) break;
        __asm__ ("pause");
    }
    if (deadlock_count >= 1000000) kpanic(NULL, "nvme driver deadlocked while polling cqe\n");

    nvme_ccmd_t out = queue->cq.data[queue->cq.head];
    queue->sq.head = out.sqhd;

    queue->cq.head++;
    if (queue->cq.head == queue->entries) {
        queue->cq.head = 0;
        // invert phase tag upon wrapping around
        queue->cq.phase = !queue->cq.phase;
    }

    *queue->cq.cqh = queue->cq.head;

    queue->sq.free_entries++;

    return out;
}

// return NULL if status=FAILURE, else return pointer to allocated identify structure
union nvme_identify_ds *nvme_issue_cmd_identify(struct nvme_controller *controller, uint8_t cns, uint16_t cntid, uint16_t nsid)
{
    // [FIXME] fix this sht (don't claim hhdm page)
    union nvme_identify_ds *identifier = pmm_claim_contiguous_pages(1);
    vmm_map_single_page(&kernel_pmc, hhdm->offset + (uintptr_t)identifier,
        (uintptr_t)identifier, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_READ_WRITE);
    identifier = (void *)((uintptr_t)identifier + hhdm->offset);
    memset(identifier, 0, PAGE_SIZE);

    nvme_scmd_t cmd = {
        .identify.opc = NVME_OPC_ADMIN_IDENTIFY,
        .identify.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        .identify.prp1 = (uintptr_t)identifier - hhdm->offset,
        .identify.prp2 = 0,
        .identify.cns = cns,
        .identify.cntid = cntid,
        .identify.cns_specific_id = 0,  // not required for supported cns'es
        .identify.csi = 0,  // nvm cmd set
        .identify.uuid = 0,
        .identify.nsid = nsid
    };
    // use prp2 if pointer is not page aligned (which should be able to happen)
    if ((uintptr_t)identifier % PAGE_SIZE) {
        kprintf("WARNING: nvme identify structure not aligned\n");
        cmd.identify.prp2 = ALIGN_UP((uintptr_t)identifier - hhdm->offset, PAGE_SIZE);
    }

    if (!nvme_queue_submit_single_cmd(&controller->aq, &cmd)) {
        kpanic(NULL, "NVME_INIT: failed to identify nvme controller\n");
    }
    nvme_ccmd_t cqe = nvme_queue_poll_single_cqe(&controller->aq);
    if (!NVME_CQE_SUCCESSFUL(cqe)) {
        nvme_debug_cqe_unsuccessful(cqe);
        return NULL;
    }

    return identifier;
}

// each controller (even if on the same drive) appears as seperate pci(e) device.
// controller types are admin, io and discovery
void init_nvme_controller(pci_device *dev)
{
#ifdef MUNKOS_DEBUG_BUILD
    nvme_cmdset_struct_verify_size();
#endif // MUNKOS_DEBUG_BUILD

    struct nvme_controller controller = {
        .active_ns = VECTOR_INIT(nvme_ns_ctx)
    };

    // interrupts enable, bus-mastering DMA, memory space access [FIXME] MMIO too?
    pci_set_command_reg(dev, PCI_CMD_REG_FLAG_BUS_MASTER | PCI_CMD_REG_FLAG_MEMORY_SPACE | PCI_CMD_REG_FLAG_MMIO);
    // map bar0
    struct pci_base_addr_reg_ctx nvme_pci_bar0 = {0};
    pci_read_bar(dev, &nvme_pci_bar0, 0);

    //if (!nvme_pci_bar0.is_mmio_bar) kpanic(NULL, "NVME_INIT: pci bar0 is not mmio mapped\n");
    for (uintptr_t ptr = ALIGN_DOWN((uintptr_t)nvme_pci_bar0.base, PAGE_SIZE);
        ptr < ALIGN_UP((uintptr_t)nvme_pci_bar0.base + nvme_pci_bar0.size, PAGE_SIZE); ptr += PAGE_SIZE) {
        vmm_unmap_single_page(&kernel_pmc, ptr + hhdm->offset, false); // do not free page
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
    controller.properties->aqa = (((uint32_t)entries & 0xFFF) - 1) | ((((uint32_t)entries & 0xFFF) - 1) << 16);

    // base addresses seem to require cc.mps alignment, 4KiB in this case
    if (POW(2, 12 + ((controller.properties->cc & (0xF << 7)) >> 7)) != PAGE_SIZE) kpanic(NULL, "NVME_INIT: cc.mps is configured incorrectly\n");
    nvme_init_queue(&controller, &controller.aq, 0, entries - 1);
    controller.properties->asq = (uint64_t)controller.aq.sq.data - hhdm->offset;
    controller.properties->acq = (uint64_t)controller.aq.cq.data - hhdm->offset;

    // reenable controller
    controller.properties->cc = controller.properties->cc | 0b1;
    while (!(controller.properties->csts & NVME_STATUS_READY)) __asm__ ("pause");
    if (controller.properties->csts & (1 << 1)) kpanic(NULL, "NVME_INIT: fatal\n");

    // register msi-x interrupt [TODO]

    // identify ctrler
    controller.ctrler_identify = nvme_issue_cmd_identify(&controller, NVME_CNS_CONTROLLER, 0, 0);
    if (!controller.ctrler_identify) kpanic(NULL, "NVME_INIT: failed to identify nvme controller\n");

    // is io ctrler?
    if (controller.ctrler_identify->ctrler.cntrltype != NVME_CTRLER_TYPE_IO) {
        kprintf("found non-io controller: type=0x%lX not supported (0x3=admin, 0x2=discovery).\n", (uint64_t)controller.ctrler_identify->ctrler.cntrltype);
        // clear up controller structures?
        return;
    }
    controller.cntlid = controller.ctrler_identify->ctrler.cntlid;

    // reset software progress marker, if supported
    uint32_t is_supported = controller.ctrler_identify->ctrler.oncs & (1 << 4);
    kprintf("nvme: software progress marker supported: %u\n", (uint32_t)is_supported);
    if (!is_supported) goto not_supported;

    nvme_scmd_t marker_cmd = {
        .set_features.opc = NVME_OPC_ADMIN_SET_FEATURES,
        .set_features.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        // save, software progress marker
        .set_features.dword_10 = (1 << 31) | 0x80,
        // reset to 0 [TODO] check old value?
        .set_features.dword_11 = 0
    };

    if (!nvme_queue_submit_single_cmd(&controller.aq, &marker_cmd)) {
        kpanic(NULL, "nvme: failed to send software progress marker cmd\n");
    }
    nvme_ccmd_t cqe = nvme_queue_poll_single_cqe(&controller.aq);
    if (!NVME_CQE_SUCCESSFUL(cqe)) {
        nvme_debug_cqe_unsuccessful(cqe);
        kprintf("nvme: [probably] non-fatal error: software progress marker could not be saved or another error occured\n");
    }

not_supported:

    // identify this cntrlers active nsids
    union nvme_identify_ds *nsid_list = nvme_issue_cmd_identify(&controller, NVME_CNS_NS_LIST, 0, 0);
    if (!nsid_list) kpanic(NULL, "NVME_INIT: Failed to identify namespaces\n");

    // reserve each one sq and cq for each namespace (maybe do this per cpu?)
    uint64_t count = next_pow_2(controller.ctrler_identify->ctrler.nn) - 1;
    nvme_scmd_t set_queues_cmd = {
        .set_features.opc = NVME_OPC_ADMIN_SET_FEATURES,
        .set_features.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        .set_features.dword_10 = 0x07,
        .set_features.dword_11 = count | (count << 16)
    };

    if (!nvme_queue_submit_single_cmd(&controller.aq, &set_queues_cmd)) {
        kpanic(NULL, "NVME_INIT: failed to send set queue count cmd\n");
    }
    nvme_ccmd_t queues_cqe = nvme_queue_poll_single_cqe(&controller.aq);
    if (!NVME_CQE_SUCCESSFUL(queues_cqe)) {
        nvme_debug_cqe_unsuccessful(queues_cqe);
        kpanic(NULL, "NVME_INIT: set queue count failed, unrecoverable\n");
    }

    uint16_t nsqa = (uint16_t)cqe.dword_0 + 1;
    uint16_t ncqa = (uint16_t)(cqe.dword_0 >> 16) + 1;
    if (nsqa != ncqa) {
        kpanic(NULL, "nvme: couldn't allocate same amount of sq and cq\n");
    }
    // zero based
    controller.queue_count = nsqa;
    kprintf_verbose("NVME_INIT: reserved %lu queues\n", controller.queue_count);

    for (size_t i = 0; i < sizeof(union nvme_identify_ds) / sizeof(uint32_t); i++) {
        if (!nsid_list->active_nsid_list.ids[i] || nsid_list->active_nsid_list.ids[i] > controller.ctrler_identify->ctrler.nn) {
            continue;
        }

        struct nvme_ns_ctx nsctx = {
            .nsid = nsid_list->active_nsid_list.ids[i]
        };

        // record mdts (same for each ns in a cntrler)
        if (controller.ctrler_identify->ctrler.mdts) {
            uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller.properties->cap) >> 48));
            nsctx.mdts = POW(2, controller.ctrler_identify->ctrler.mdts) * mpsmin;
        } else {
            nsctx.mdts = ~0ul;
        }
        kprintf_verbose("nvme: maximum transfer size 0x%lX\n", nsctx.mdts);

        // record each ns block size, capacity, read-only (not yet implemented)
        union nvme_identify_ds *ns_ident = nvme_issue_cmd_identify(&controller, NVME_CNS_NAMESPACE, 0, nsid_list->active_nsid_list.ids[i]);
        if (!ns_ident) kpanic(NULL, "NVME_INIT: Failed to identify namespace %u\n", nsid_list->active_nsid_list.ids[i]);

        nsctx.ident = ns_ident;

        // implement optperf?

        kprintf_verbose("nvme: namespace: nsze=%u lb; ncap=%lu / nuse=%lu; nlbaf=%lu; flbas=%lu\n",
            (uint64_t)ns_ident->namespace.nsze, (uint64_t)ns_ident->namespace.ncap, (uint64_t)ns_ident->namespace.nuse,
            (uint64_t)ns_ident->namespace.nlbaf, (uint64_t)ns_ident->namespace.flbas);

        uint8_t fmt_idx = ns_ident->namespace.flbas & 0xF;
        if (ns_ident->namespace.nlbaf > 16) {
            fmt_idx += (ns_ident->namespace.flbas >> 1) & (0x3 << 4);
        }

        nsctx.lba_size = POW(2, ns_ident->namespace.lbaf[fmt_idx].lbads);
        nsctx.cap = ns_ident->namespace.nsze;
        kprintf_verbose("nvme: lba_size=%lu, cap=%lu\n", nsctx.lba_size, nsctx.cap);

        controller.active_ns.push_back(&controller.active_ns, &nsctx);
    }

    // allocate 1 sq and cq per namespace (for now)
    for (size_t i = 0; i < controller.active_ns.size && i < controller.queue_count; i++) {
        struct nvme_queue_ctx *this_queue = &controller.active_ns.data[i].queue;
        // id n + 1 because aq
        nvme_init_queue(&controller, this_queue, i + 1, entries);
        kprintf("%lu\n", entries);
        
        nvme_scmd_t ciocq = {
            .create_io_cq.opc = NVME_OPC_ADMIN_CRT_IO_CQ,
            .create_io_cq.flags = NVME_CMD_FLAGS_FUSE_NORMAL | NVME_CMD_FLAGS_PRPS,
            .create_io_cq.prp1 = (uintptr_t)this_queue->cq.data - hhdm->offset,
            .create_io_cq.qsize = entries - 1,
            .create_io_cq.qid = i + 1,
            .create_io_cq.iv = 0,
            .create_io_cq.flags_2 = 0b01    // ints disable, phys contig
        };

        nvme_scmd_t ciosq = {
            .create_io_sq.opc = NVME_OPC_ADMIN_CRT_IO_SQ,
            .create_io_sq.flags = NVME_CMD_FLAGS_FUSE_NORMAL | NVME_CMD_FLAGS_PRPS,
            .create_io_sq.prp1 = (uintptr_t)this_queue->sq.data - hhdm->offset,
            .create_io_sq.qsize = entries - 1,
            .create_io_sq.qid = i + 1,
            .create_io_sq.cqid = i + 1,
            .create_io_sq.flags_2 = 0b01,   // URGENT prio, phys contig
            .create_io_sq.nvmesetid = 0
        };

        if (!nvme_queue_submit_single_cmd(&controller.aq, &ciocq)) {
            kpanic(NULL, "NVME_INIT: failed to send ciocq cmd\n");
        }
        if (!nvme_queue_submit_single_cmd(&controller.aq, &ciosq)) {
            kpanic(NULL, "NVME_INIT: failed to send ciosq cmd\n");
        }

        nvme_ccmd_t cq_cqe = nvme_queue_poll_single_cqe(&controller.aq);
        if (!NVME_CQE_SUCCESSFUL(cq_cqe)) {
            nvme_debug_cqe_unsuccessful(cq_cqe);
            kpanic(NULL, "NVME_INIT: couldn't create cq\n");
        }
        nvme_ccmd_t sq_cqe = nvme_queue_poll_single_cqe(&controller.aq);
        if (!NVME_CQE_SUCCESSFUL(sq_cqe)) {
            nvme_debug_cqe_unsuccessful(sq_cqe);
            kpanic(NULL, "NVME_INIT: couldn't create sq\n");
        }

        // delete queues?
    }

    kprintf("NVME_INIT: initialized nvme controller.\n");

    // testing

    void *buffer = kmalloc(512);
    if (!nvme_issue_cmd_read_blocking(&controller.active_ns.data[0], 0, 1, buffer)) kpanic(NULL, NULL);
    kprintf("nvme_testing: read data: ");
    for (size_t i = 0; i < 512 / sizeof(char); i++) {
        kprintf("%c", ((char *)buffer)[i]);
    }
    kprintf("\n");
}

// [TODO] rewrite prp setting
static bool nvme_rw_blocking(struct nvme_ns_ctx *namespace, uint64_t starting_lba, uint64_t blocks, void *buffer, uint8_t opc)
{
    if (opc != NVME_OPC_IO_READ && opc != NVME_OPC_IO_WRITE) kpanic(NULL, "nvme: invalid opcode for r/w\n");
    nvme_scmd_t read = {
        .read.opc = opc,
        .read.flags = NVME_CMD_FLAGS_FUSE_NORMAL | NVME_CMD_FLAGS_PRPS,
        .read.nsid = namespace->nsid,
        .read.slba = starting_lba,
        .read.nlb = blocks - 1
    };

    // for now, create a prp list even if not necessarily needed, but who cares
    uintptr_t buf_aligned = ALIGN_UP((uintptr_t)buffer, PAGE_SIZE);
    const size_t prp_entries = ((blocks - 1) * namespace->lba_size) / PAGE_SIZE;
    // prp_list needs to be physically contiguous
    uintptr_t prp_list[prp_entries];
    for (size_t i = 0; i < prp_entries; i++) {
        prp_list[i] = virt2phys(&kernel_pmc, (uintptr_t)buf_aligned + i * PAGE_SIZE);
    }
    read.read.prp1 = virt2phys(&kernel_pmc, (uintptr_t)buffer);
    read.read.prp2 = virt2phys(&kernel_pmc, (uintptr_t)prp_list);

    if (!nvme_queue_submit_single_cmd(&namespace->queue, &read)) {
        kpanic(NULL, "nvme: critical error occured while attempting to issue read command\n");
    }

    nvme_ccmd_t cqe = nvme_queue_poll_single_cqe(&namespace->queue);
    if (!NVME_CQE_SUCCESSFUL(cqe)) {
        nvme_debug_cqe_unsuccessful(cqe);
        kpanic(NULL, "nvme: critical error occured during reading from disk\n");
    }

    return 1;
}

// return success, ask controller to read blocks logical blocks starting at starting_lba into buf, blocking
inline bool nvme_issue_cmd_read_blocking(struct nvme_ns_ctx *namespace, uint64_t starting_lba, uint64_t blocks, void *buffer) {
    return nvme_rw_blocking(namespace, starting_lba, blocks, buffer, NVME_OPC_IO_READ);
}

// return success, ask controller to write blocks logical blocks starting at starting_lba from buf to disk, blocking
inline bool nvme_issue_cmd_write_blocking(struct nvme_ns_ctx *namespace, uint64_t starting_lba, uint64_t blocks, void *buffer) {
    return nvme_rw_blocking(namespace, starting_lba, blocks, buffer, NVME_OPC_IO_WRITE);
}