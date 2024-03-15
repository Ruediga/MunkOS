#include "nvme.h"
#include "macros.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "interrupt.h"
#include "kprintf.h"
#include "kheap.h"
#include "math.h"
#include "device.h"
#include "disk_partition.h"

VECTOR_TMPL_TYPE_NON_NATIVE(nvme_ns_ctx)

static int nvme_read(struct device *dev, void *buf, size_t off, size_t count);
static int nvme_write(struct device *dev, void *buf, size_t off, size_t count);
static bool nvme_rw_blocking(struct nvme_ns_ctx *namespace, uint64_t starting_lba, uint64_t blocks, void *buffer, uint8_t opc);

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

    if (failed) kpanic(0, NULL, "NVME_INIT: structure size check failed:\n");
}
#endif // MUNKOS_DEBUG_BUILD

static void nvme_cache_container_init(struct nvme_cache_container *container)
{
    for (size_t i = 0; i < NVME_NCACHES; i++) {
        container->cache[i].status = NVME_CACHE_EMPTY;
        container->cache[i].bid = NVME_IDX_INV;
        container->cache[i].block = NULL;
    }

    for (size_t i = 0; i < NVME_HASHMAP_SIZE; i++) {
        container->hashmap[i].bid = NVME_IDX_INV;
        container->hashmap[i].index = NVME_IDX_INV;
        container->hashmap[i].next = NULL;
    }

    container->head = container->tail = NULL;
    for (size_t i = 0; i < NVME_NCACHES; i++) {
        struct nvme_cache_dll_node *new = kmalloc(sizeof(struct nvme_cache_dll_node));
        new->next = NULL;
        new->prev = NULL;
        new->index = i;
        container->cache[i].lruref = new;

        // set tail on first iteration
        if (i == 0) container->tail = new;
        struct nvme_cache_dll_node *old = container->head;
        
        container->head = new;
        new->prev = NULL;
        new->next = old;
        new->index = i;

        if (old) old->prev = new;
    }
}

static void nvme_cache_container_cleanup(struct nvme_cache_container *container)
{
    struct nvme_cache_dll_node *node = container->head;
    while (node) {
        struct nvme_cache_dll_node *cpy = node;
        node = node->next;
        kfree(container->cache[cpy->index].block);
        kfree(cpy);
    }
}

// call if some block isn't yet cached
// overwrite lru entry, return it's index
static size_t nvme_cache_lru_evict(struct nvme_cache_container *container)
{
    size_t index = container->tail->index;
    if (container->cache[index].status == NVME_CACHE_EMPTY) {
        container->cache[index].block = kmalloc(NVME_BLOCK_SIZE);
        container->cache[index].status = NVME_CACHE_VALID;
    }
    return index;
}

// call on successful read into an existing or evicted cache block
// updates entry at index in queue
static void nvme_cache_inform_usage(struct nvme_cache_container *container, size_t index)
{
    struct nvme_cache_dll_node *node = container->cache[index].lruref;
    
    if (node == container->head) {
        return;
    }

    // unlink it
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }

    if (node == container->tail) {
        container->tail = node->prev;
    }

    // insert it at the front
    node->next = container->head;
    node->prev = NULL;
    container->head->prev = node;
    container->head = node;
}

// jenkins hash. why? because it was the first result on google
static inline uint64_t nvme_cache_hash_func(uint64_t in)
{
    in = (in ^ 1234567890) + (in << 12);
    in = (in ^ 0xabcdef01) ^ (in >> 9);
    in = (in + 0x6543210) ^ (in << 6);
    in = (in ^ 0xdeadbeef) + (in >> 15);
    return in % NVME_HASHMAP_SIZE;
}

// if old_bid == NVME_IDX_INV: add new entry for bid with the value index
// else: remove old_bid mapping, --||--
static void nvme_cache_hashmap_update(struct nvme_cache_hashmap_entry *hashmap, uint64_t old_bid, uint64_t bid, uint64_t index)
{
    if (old_bid != NVME_IDX_INV) {
        uint64_t old_idx = nvme_cache_hash_func(old_bid);

        if (hashmap[old_idx].bid == old_bid) {
            // if it's in the array
            hashmap[old_idx].bid = NVME_IDX_INV;
            hashmap[old_idx].index = NVME_IDX_INV;
        } else {
            struct nvme_cache_hashmap_entry *item = &hashmap[old_idx];
            while (item->next->bid != old_bid) {
                item = item->next;
            }
            struct nvme_cache_hashmap_entry *next = item->next->next;
            kfree(item->next);
            item->next = next;
        }
    }

    uint64_t idx = nvme_cache_hash_func(bid);

    if (hashmap[idx].bid == NVME_IDX_INV) {
        // if place in array is free
        hashmap[idx].bid = bid;
        hashmap[idx].index = index;
    } else {
        struct nvme_cache_hashmap_entry *old_next = hashmap[idx].next;
        hashmap[idx].next = kmalloc(sizeof(struct nvme_cache_hashmap_entry));
        hashmap[idx].next->bid = bid;
        hashmap[idx].next->index = index; 
        hashmap[idx].next->next = old_next;
    }
}

// return NVME_IDX_INV if not found, else return cache_entries[] index
static uint64_t nvme_cache_hashmap_find(struct nvme_cache_hashmap_entry *hashmap, uint64_t bid)
{
    uint64_t idx = nvme_cache_hash_func(bid);
    struct nvme_cache_hashmap_entry *pos = &hashmap[idx];
    while (pos->bid != bid) {
        pos = pos->next;
        if (!pos) return NVME_IDX_INV;
    }
    return pos->index;
}

// cache block, return index of cache_entry corresponding to this cache
// prep_write_full_block should be used if a full block is about to be written.
// prep_read_block should be used for any reading operations or for smaller than full block writes.
static size_t nvme_read_block(struct nvme_ns_ctx *ns, size_t block)
{
    struct nvme_cache_container *container = &ns->cache_container;
    size_t index = nvme_cache_hashmap_find(container->hashmap, block);

    if (index != NVME_IDX_INV) {
        kprintf("read: found cached block %lu at index %lu\n", block, index);
        nvme_cache_inform_usage(container, index);
        return index;
    } else {
        size_t evicted_idx = nvme_cache_lru_evict(container);
        if (container->cache[evicted_idx].status == NVME_CACHE_DIRTY) {
            if (!nvme_rw_blocking(ns, (NVME_BLOCK_SIZE / ns->lba_size) * container->cache[evicted_idx].bid, (NVME_BLOCK_SIZE / ns->lba_size),  container->cache[evicted_idx].block, NVME_OPC_IO_WRITE)) {
                kprintf_verbose("Failed to write block %lu to disk\n", container->cache[evicted_idx].bid);
                return NVME_IDX_INV;
            }
            kprintf_verbose("Wrote block %lu to disk\n", container->cache[evicted_idx].bid);
        }
        if (!nvme_rw_blocking(ns, (NVME_BLOCK_SIZE / ns->lba_size) * block, (NVME_BLOCK_SIZE / ns->lba_size), container->cache[evicted_idx].block, NVME_OPC_IO_READ)) {
            kprintf_verbose("Failed to read block %lu from disk\n", block);
            return NVME_IDX_INV;
        }
        kprintf_verbose("Read block %lu from disk\n", block);

        nvme_cache_hashmap_update(container->hashmap, container->cache[evicted_idx].bid, block, evicted_idx);
        container->cache[evicted_idx].bid = block;
        container->cache[evicted_idx].status = NVME_CACHE_VALID;
        nvme_cache_inform_usage(container, evicted_idx);
        return evicted_idx;
    }
}

// cache block, return index of cache_entry corresponding to this cache
// prep_write_full_block should be used if a full block is about to be written.
// prep_read_block should be used for any reading operations or for smaller than full block writes.
static size_t nvme_prep_write_full_block(struct nvme_ns_ctx *ns, size_t block)
{
    struct nvme_cache_container *container = &ns->cache_container;
    size_t index = nvme_cache_hashmap_find(container->hashmap, block);

    if (index != NVME_IDX_INV) {
        kprintf("write: found cached block %lu at index %lu\n", block, index);
        if (container->cache[index].status == NVME_CACHE_VALID) {
            container->cache[index].status = NVME_CACHE_DIRTY;
        }
        nvme_cache_inform_usage(container, index);
        return index;
    } else {
        size_t evicted_idx = nvme_cache_lru_evict(container);
        if (container->cache[evicted_idx].status == NVME_CACHE_DIRTY) {
            if (!nvme_rw_blocking(ns, (NVME_BLOCK_SIZE / ns->lba_size) * container->cache[evicted_idx].bid, (NVME_BLOCK_SIZE / ns->lba_size),  container->cache[evicted_idx].block, NVME_OPC_IO_WRITE)) {
                kprintf_verbose("Failed to write block %lu to disk\n", container->cache[evicted_idx].bid);
                return NVME_IDX_INV;
            }
            kprintf_verbose("Wrote block %lu to disk\n", container->cache[evicted_idx].bid);
        }
        kprintf_verbose("Wrote block %lu to evicted cache\n", block);

        nvme_cache_hashmap_update(container->hashmap, container->cache[evicted_idx].bid, block, evicted_idx);
        container->cache[evicted_idx].bid = block;
        container->cache[evicted_idx].status = NVME_CACHE_DIRTY;
        nvme_cache_inform_usage(container, evicted_idx);
        return evicted_idx;
    }
}

// not necessarily synchronous syncing
static void nvme_flush_blocks(struct nvme_cache_container *container)
{
    struct nvme_cache_dll_node *node = container->head;
    while (node) {
        node = node->next;
        // schedule this and do it async?
        if (container->cache[node->index].status == NVME_CACHE_DIRTY) {
            // write block to disk
            container->cache[node->index].status = NVME_CACHE_VALID;
        }
    }
}

static void nvme_debug_cqe_unsuccessful(nvme_ccmd_t cqe)
{
    kprintf("nvme: cqe failed!\ncid = %u, phase = %u, status = %u [sct=0x%X, sc=0x%X], sqid = %u\n",
        (uint32_t)cqe.cid, (uint32_t)cqe.status & 1, (uint32_t)cqe.status >> 1,
        (((uint32_t)cqe.status >> 1) >> 8) & 0b111, ((uint32_t)cqe.status >> 1) & 0xFF, (uint32_t)cqe.sqid);
}

static void nvme_compatibility_check(struct nvme_controller *controller)
{
    // 4 KiB page size
    uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller->properties->cap) >> 48));
    uint64_t mpsmax = POW(2ul, 12 + (((0xFul << 52) & controller->properties->cap) >> 52));
    if (!(mpsmin <= PAGE_SIZE && mpsmax >= PAGE_SIZE)) {
        kpanic(0, NULL, "NVME_INIT: controller doesn't support host page size (mpsmin=%lX, mpsmax=%lX)\n", mpsmin, mpsmax);
    }
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

    // unnecessary for admin queue but who cares
    queue->prp_list_container = kcalloc(entries, sizeof(uint64_t));
}

// returns success (0 = error), generic queue manipulation to append new sq entry
static bool nvme_queue_submit_single_cmd(struct nvme_queue_ctx *queue, nvme_scmd_t *cmd)
{
    // if sq if full, do not submit entry, and abort
    if (queue->sq.free_entries == 0) return false;

    // append new cmd
    queue->sq.data[queue->sq.tail++] = *(cmd);
    queue->sq.tail = queue->sq.tail % queue->entries;

    // update cid (upon word overflow, conviniently resets to 0)
    cmd->common_command_format_entry.cid = queue->sq.cid_counter++;

    // update doorbell
    *queue->sq.sqt = queue->sq.tail;

    queue->sq.free_entries--;

    return true;
}

// instead of waiting for an interrupt, manually poll from the queue
static nvme_ccmd_t nvme_queue_poll_single_cqe(struct nvme_queue_ctx *queue)
{
    uint16_t status;

    size_t deadlock_count = 0;
    for (; deadlock_count <= 1000000; deadlock_count++) {
        status = queue->cq.data[queue->cq.head].status;
        if ((status & 1) == queue->cq.phase) break;
        __asm__ ("pause");
    }
    if (deadlock_count >= 1000000) kpanic(0, NULL, "nvme driver deadlocked while polling cqe\n");

    nvme_ccmd_t out = queue->cq.data[queue->cq.head];
    queue->sq.head = out.sqhd;

    // free prp list if one was used
    uintptr_t cpy = queue->prp_list_container[out.cid % queue->entries];
    if (cpy != 0) {
        vmm_unmap_single_page(&kernel_pmc, cpy, true);
        queue->prp_list_container[out.cid % queue->entries] = 0;
    }

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
        kpanic(0, NULL, "NVME_INIT: failed to identify nvme controller\n");
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

    struct nvme_controller *controller = kmalloc(sizeof(struct nvme_controller));
    VECTOR_REINIT(controller->active_ns, nvme_ns_ctx);

    // interrupts enable, bus-mastering DMA, memory space access [FIXME] MMIO too?
    pci_set_command_reg(dev, PCI_CMD_REG_FLAG_BUS_MASTER | PCI_CMD_REG_FLAG_MEMORY_SPACE | PCI_CMD_REG_FLAG_MMIO);
    // map bar0
    struct pci_base_addr_reg_ctx nvme_pci_bar0 = {0};
    pci_read_bar(dev, &nvme_pci_bar0, 0);

    //if (!nvme_pci_bar0.is_mmio_bar) kpanic(0, NULL, "NVME_INIT: pci bar0 is not mmio mapped\n");
    for (uintptr_t ptr = ALIGN_DOWN((uintptr_t)nvme_pci_bar0.base, PAGE_SIZE);
        ptr < ALIGN_UP((uintptr_t)nvme_pci_bar0.base + nvme_pci_bar0.size, PAGE_SIZE); ptr += PAGE_SIZE) {
        vmm_unmap_single_page(&kernel_pmc, ptr + hhdm->offset, false); // do not free page
        vmm_map_single_page(&kernel_pmc, ptr + hhdm->offset, ptr,
            PTE_BIT_DISABLE_CACHING | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    }

    controller->properties = (volatile struct nvme_controller_properties *)((uintptr_t)nvme_pci_bar0.base + hhdm->offset);

    if (!controller->properties->version) kpanic(0, NULL, "NVME_INIT: invalid controller version\n");

    // compatibility checks
    nvme_compatibility_check(controller);

    // reset controller
    nvme_controller_reset(controller->properties);

    // set controller config: 16 byte (2⁴) cq entries, 64 byte (2⁶) sq entries, RR AMS (supported by all ctrlers,
    // for other algos check cap.ams), 4KiB pages (2^12+n), support NVMe cmd set (admin and io)
    controller->properties->cc = (0b100 << 20) | (0b110 << 16) | (0 << 11) | (0 << 7) | (0 << 4);

    // set aqa and asq/acq
    // attributes: asqs and acqs max size: 4 KiB (0 based!)
    uint16_t entries = NVME_PROPERTIES_MQES(controller->properties) + 1;
    controller->properties->aqa = (((uint32_t)entries & 0xFFF) - 1) | ((((uint32_t)entries & 0xFFF) - 1) << 16);

    // base addresses seem to require cc.mps alignment, 4KiB in this case
    if (POW(2, 12 + ((controller->properties->cc & (0xF << 7)) >> 7)) != PAGE_SIZE) kpanic(0, NULL, "NVME_INIT: cc.mps is configured incorrectly\n");
    nvme_init_queue(controller, &controller->aq, 0, entries - 1);
    controller->properties->asq = (uint64_t)controller->aq.sq.data - hhdm->offset;
    controller->properties->acq = (uint64_t)controller->aq.cq.data - hhdm->offset;

    // reenable controller
    controller->properties->cc = controller->properties->cc | 0b1;
    while (!(controller->properties->csts & NVME_STATUS_READY)) __asm__ ("pause");
    if (controller->properties->csts & (1 << 1)) kpanic(0, NULL, "NVME_INIT: fatal\n");

    // register msi-x interrupt [TODO]

    // identify ctrler
    controller->ctrler_identify = nvme_issue_cmd_identify(controller, NVME_CNS_CONTROLLER, 0, 0);
    if (!controller->ctrler_identify) kpanic(0, NULL, "NVME_INIT: failed to identify nvme controller\n");

    // is io ctrler?
    if (controller->ctrler_identify->ctrler.cntrltype != NVME_CTRLER_TYPE_IO) {
        kprintf("found non-io controller: type=0x%lX not supported (0x3=admin, 0x2=discovery).\n", (uint64_t)controller->ctrler_identify->ctrler.cntrltype);
        // clear up controller structures?
        return;
    }
    controller->cntlid = controller->ctrler_identify->ctrler.cntlid;

    // reset software progress marker, if supported
    uint32_t is_supported = controller->ctrler_identify->ctrler.oncs & (1 << 4);
    kprintf("nvme: software progress marker supported: %u\n", (uint32_t)is_supported);
    if (!is_supported) goto not_supported;

    nvme_scmd_t marker_cmd = {
        .set_features.opc = NVME_OPC_ADMIN_SET_FEATURES,
        .set_features.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        // save, software progress marker
        .set_features.dword_10 = (1 << 31) | 0x80,
        // reset to 0, check old value for boot fail?
        .set_features.dword_11 = 0
    };

    if (!nvme_queue_submit_single_cmd(&controller->aq, &marker_cmd)) {
        kpanic(0, NULL, "nvme: failed to send software progress marker cmd\n");
    }
    nvme_ccmd_t cqe = nvme_queue_poll_single_cqe(&controller->aq);
    if (!NVME_CQE_SUCCESSFUL(cqe)) {
        nvme_debug_cqe_unsuccessful(cqe);
        kprintf("nvme: [probably] non-fatal error: software progress marker could not be saved or another error occured\n");
    }

not_supported:

    // identify this cntrlers active nsids
    union nvme_identify_ds *nsid_list = nvme_issue_cmd_identify(controller, NVME_CNS_NS_LIST, 0, 0);
    if (!nsid_list) kpanic(0, NULL, "NVME_INIT: Failed to identify namespaces\n");

    // reserve each one sq and cq for each namespace (maybe do this per cpu?)
    uint64_t count = next_pow_2(controller->ctrler_identify->ctrler.nn) - 1;
    nvme_scmd_t set_queues_cmd = {
        .set_features.opc = NVME_OPC_ADMIN_SET_FEATURES,
        .set_features.flags = NVME_CMD_FLAGS_PRPS | NVME_CMD_FLAGS_FUSE_NORMAL,
        .set_features.dword_10 = 0x07,
        .set_features.dword_11 = count | (count << 16)
    };

    if (!nvme_queue_submit_single_cmd(&controller->aq, &set_queues_cmd)) {
        kpanic(0, NULL, "NVME_INIT: failed to send set queue count cmd\n");
    }
    nvme_ccmd_t queues_cqe = nvme_queue_poll_single_cqe(&controller->aq);
    if (!NVME_CQE_SUCCESSFUL(queues_cqe)) {
        nvme_debug_cqe_unsuccessful(queues_cqe);
        kpanic(0, NULL, "NVME_INIT: set queue count failed, unrecoverable\n");
    }

    uint16_t nsqa = (uint16_t)cqe.dword_0 + 1;
    uint16_t ncqa = (uint16_t)(cqe.dword_0 >> 16) + 1;
    if (nsqa != ncqa) {
        kpanic(0, NULL, "nvme: couldn't allocate same amount of sq and cq\n");
    }
    // zero based
    controller->queue_count = nsqa;
    kprintf_verbose("NVME_INIT: reserved %lu queues\n", controller->queue_count);

    struct device *tempdev = NULL;
    for (size_t i = 0; i < sizeof(union nvme_identify_ds) / sizeof(uint32_t); i++) {
        if (!nsid_list->active_nsid_list.ids[i] || nsid_list->active_nsid_list.ids[i] > controller->ctrler_identify->ctrler.nn) {
            continue;
        }

        struct nvme_ns_ctx nsctx = {
            .nsid = nsid_list->active_nsid_list.ids[i],
            .controller = controller
        };

        nvme_cache_container_init(&nsctx.cache_container);

        // record mdts (same for each ns in a cntrler)
        if (controller->ctrler_identify->ctrler.mdts) {
            uint64_t mpsmin = POW(2ul, 12 + (((0xFul << 48) & controller->properties->cap) >> 48));
            nsctx.mdts = POW(2, controller->ctrler_identify->ctrler.mdts) * mpsmin;
        } else {
            nsctx.mdts = ~0ul;
        }
        kprintf_verbose("nvme: maximum transfer size 0x%lX\n", nsctx.mdts);

        // record each ns block size, capacity, read-only (not yet implemented)
        union nvme_identify_ds *ns_ident = nvme_issue_cmd_identify(controller, NVME_CNS_NAMESPACE, 0, nsid_list->active_nsid_list.ids[i]);
        if (!ns_ident) kpanic(0, NULL, "NVME_INIT: Failed to identify namespace %u\n", nsid_list->active_nsid_list.ids[i]);

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

        controller->active_ns.push_back(&controller->active_ns, &nsctx);

        // init namespace and partition devices
        // [TODO] controller devices
        struct device *nvme_ns_dev = kmalloc(sizeof(struct device));
        // store nsctx pointer in gendptr
        device_init(nvme_ns_dev, nsctx.lba_size, nsctx.cap, 0, DEV_BLOCK,
            (gen_dptr)&controller->active_ns.data[i], "nvme%un%lu", (uint32_t)nsctx.controller->cntlid, i + 1);
        // [TODO] add to devtmpfs
        nvme_ns_dev->read = nvme_read;
        nvme_ns_dev->write = nvme_write;

        tempdev = nvme_ns_dev;
    }

    // allocate 1 sq and cq per namespace (for now)
    for (size_t i = 0; i < controller->active_ns.size && i < controller->queue_count; i++) {
        struct nvme_queue_ctx *this_queue = &controller->active_ns.data[i].queue;
        // id n + 1 because aq
        nvme_init_queue(controller, this_queue, i + 1, entries);
        
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

        if (!nvme_queue_submit_single_cmd(&controller->aq, &ciocq)) {
            kpanic(0, NULL, "NVME_INIT: failed to send ciocq cmd\n");
        }
        if (!nvme_queue_submit_single_cmd(&controller->aq, &ciosq)) {
            kpanic(0, NULL, "NVME_INIT: failed to send ciosq cmd\n");
        }

        nvme_ccmd_t cq_cqe = nvme_queue_poll_single_cqe(&controller->aq);
        if (!NVME_CQE_SUCCESSFUL(cq_cqe)) {
            nvme_debug_cqe_unsuccessful(cq_cqe);
            kpanic(0, NULL, "NVME_INIT: couldn't create cq\n");
        }
        nvme_ccmd_t sq_cqe = nvme_queue_poll_single_cqe(&controller->aq);
        if (!NVME_CQE_SUCCESSFUL(sq_cqe)) {
            nvme_debug_cqe_unsuccessful(sq_cqe);
            kpanic(0, NULL, "NVME_INIT: couldn't create sq\n");
        }
    }

    kprintf("NVME_INIT: initialized nvme controller->DEVICE\n");

    partition_disk_device(tempdev);
}

// read/write blocks lbas starting at starting_lba into buffer
static bool nvme_rw_blocking(struct nvme_ns_ctx *namespace, uint64_t starting_lba, uint64_t blocks, void *buffer, uint8_t opc)
{
    if (opc != NVME_OPC_IO_READ && opc != NVME_OPC_IO_WRITE) {
        kprintf_verbose("nvme: invalid opcode for read/write");
        return 0;
    }
    if (blocks > namespace->mdts || (blocks * namespace->lba_size) > (PAGE_SIZE * namespace->queue.entries)) {
        kprintf_verbose("nvme: crossed maximum transfer size or max implemented prp_list length");
        return 0;
    }
    
    nvme_scmd_t read = {
        .read.opc = opc,
        .read.flags = NVME_CMD_FLAGS_FUSE_NORMAL | NVME_CMD_FLAGS_PRPS,
        .read.nsid = namespace->nsid,
        .read.slba = starting_lba,
        .read.nlb = blocks - 1  // 0-indexed
    };

    uintptr_t buffer_start = virt2phys(&kernel_pmc, (uintptr_t)buffer);
    uint64_t length = blocks * namespace->lba_size;

    // this is left unoptimized because debugging issues with prps is tedious
    if (!crosses_boundary(buffer_start, length, PAGE_SIZE)) {
        // prp1 if no page boundary crossed
        read.read.prp1 = buffer_start;
    } else {
        if ((length >= (PAGE_SIZE * 2) && (buffer_start % PAGE_SIZE) != 0) || (length > (PAGE_SIZE * 2) && (buffer_start % PAGE_SIZE) == 0)) {
            // prp2 prp list pointer (this solution is very suboptimal)
            uintptr_t prp_list = (uintptr_t)pmm_claim_contiguous_pages(1);
            vmm_map_single_page(&kernel_pmc, prp_list + hhdm->offset, prp_list, PTE_BIT_EXECUTE_DISABLE | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
            prp_list += hhdm->offset;

            namespace->queue.prp_list_container[namespace->queue.sq.cid_counter % namespace->queue.entries] = prp_list;

            uintptr_t pos = ALIGN_UP((uintptr_t)buffer, PAGE_SIZE);
            for (size_t i = 0; pos < (uintptr_t)buffer + length; pos += PAGE_SIZE, i++) {
                ((uintptr_t *)prp_list)[i] = virt2phys(&kernel_pmc, pos);
            }
            if ((buffer_start % PAGE_SIZE) != 0) {
                // if unaligned
                read.read.prp1 = buffer_start;
            }
        } else {
            // prp1 & prp2
            read.read.prp1 = buffer_start;
            read.read.prp2 = virt2phys(&kernel_pmc, ALIGN_UP((uintptr_t)buffer, PAGE_SIZE));
        }
    }

    if (!nvme_queue_submit_single_cmd(&namespace->queue, &read)) {
        kprintf_verbose("nvme: critical error occured while attempting to issue read command\n");
        return 0;
    }

    nvme_ccmd_t cqe = nvme_queue_poll_single_cqe(&namespace->queue);
    if (!NVME_CQE_SUCCESSFUL(cqe)) {
        kprintf_verbose("nvme: critical error occured during reading from disk\n");
        nvme_debug_cqe_unsuccessful(cqe);
        return 0;
    }

    return 1;
}

// Device read function. Read count bytes starting at off into buf. These may read from cache.
// (planned) Avoid caching with huge batched workloads and read them asynchronously
int nvme_read(struct device *dev, void *buf, size_t off, size_t count)
{
    acquire_lock(&dev->lock);
    struct nvme_ns_ctx *ns = (struct nvme_ns_ctx *)dev->dev_specific;

    // for non-full blocks perform a read and partially copy cacheblock into buffer
    // for full blocks perform a read and copy full cacheblock into buffer
    size_t block_lo = ALIGN_UP(off, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE;
    size_t block_hi = ALIGN_DOWN(off + count, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE - 1;
    size_t bytes_read = 0;

    if (off % NVME_BLOCK_SIZE) {
        size_t unaligned_front = nvme_read_block(ns, ALIGN_DOWN(off, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE);
        if (unaligned_front == NVME_IDX_INV) goto fail;
        uint64_t remaining = min(count, NVME_BLOCK_SIZE - (off % NVME_BLOCK_SIZE));
        memcpy(buf, ns->cache_container.cache[unaligned_front].block + (off % NVME_BLOCK_SIZE), remaining);
        bytes_read += remaining;
    }

    for (size_t iter = 0, block = block_lo; block <= block_hi; iter++, block++) {
        size_t i = nvme_read_block(ns, block);
        if (i == NVME_IDX_INV) goto fail;
        memcpy(buf + bytes_read, ns->cache_container.cache[i].block, NVME_BLOCK_SIZE);
        bytes_read += NVME_BLOCK_SIZE;
    }

    if (bytes_read < count) {
        size_t unaligned_end = nvme_read_block(ns, block_hi + 1);
        if (unaligned_end == NVME_IDX_INV) goto fail;
        memcpy(buf + bytes_read, ns->cache_container.cache[unaligned_end].block, count - bytes_read);
        bytes_read += count - bytes_read;
    }

fail:
    release_lock(&dev->lock);
    return bytes_read;
}

// Device write function. Write count bytes starting at off into buf. These may written to cache.
// (planned) Avoid caching with huge batched workloads and write them asynchronously
int nvme_write(struct device *dev, void *buf, size_t off, size_t count)
{
    acquire_lock(&dev->lock);
    struct nvme_ns_ctx *ns = (struct nvme_ns_ctx *)dev->dev_specific;

    // for non-full blocks perform a read and partially write buffer into cacheblock
    // for full blocks perform a prep_write and copy full buffer into cacheblock

    size_t block_lo = ALIGN_UP(off, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE;
    size_t block_hi = ALIGN_DOWN(off + count, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE - 1;
    size_t bytes_written = 0;

    if (off % NVME_BLOCK_SIZE) {
        size_t unaligned_front = nvme_read_block(ns, ALIGN_DOWN(off, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE);
        if (unaligned_front == NVME_IDX_INV) goto fail;
        uint64_t remaining = min(count, NVME_BLOCK_SIZE - (off % NVME_BLOCK_SIZE));
        memcpy(ns->cache_container.cache[unaligned_front].block + (off % NVME_BLOCK_SIZE), buf, remaining);
        bytes_written += remaining;
    }

    for (size_t iter = 0, block = block_lo; block <= block_hi; iter++, block++) {
        size_t i = nvme_prep_write_full_block(ns, block);
        if (i == NVME_IDX_INV) goto fail;
        memcpy(ns->cache_container.cache[i].block, buf + bytes_written, NVME_BLOCK_SIZE);
        bytes_written += NVME_BLOCK_SIZE;
    }

    if (bytes_written < count) {
        size_t unaligned_end = nvme_read_block(ns, ALIGN_DOWN(off + count, NVME_BLOCK_SIZE) / NVME_BLOCK_SIZE);
        if (unaligned_end == NVME_IDX_INV) goto fail;
        memcpy(ns->cache_container.cache[unaligned_end].block, buf + bytes_written, count - bytes_written);
        bytes_written += count - bytes_written;
    }

fail:
    release_lock(&dev->lock);
    return bytes_written;
}