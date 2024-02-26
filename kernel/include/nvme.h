#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// most admin and io commands
// admin command set
#define NVME_OPC_ADMIN_DEL_IO_SQ 0x0
#define NVME_OPC_ADMIN_CRT_IO_SQ 0x1
#define NVME_OPC_ADMIN_GET_LOG_PG 0x2
#define NVME_OPC_ADMIN_DEL_IO_CQ 0x4
#define NVME_OPC_ADMIN_CRT_IO_CQ 0x5
#define NVME_OPC_ADMIN_IDENTIFY 0x6
#define NVME_OPC_ADMIN_ABORT 0x8
#define NVME_OPC_ADMIN_SET_FEATURES 0x9
#define NVME_OPC_ADMIN_GET_FEATURES 0xA
#define NVME_OPC_ADMIN_ASYNC_EV_RQST 0xC
#define NVME_OPC_ADMIN_NS_MNGMT 0xD
#define NVME_OPC_ADMIN_FW_COMMIT 0x10
#define NVME_OPC_ADMIN_FW_IMG_DOWNLOAD 0x11
#define NVME_OPC_ADMIN_DEV_SELF_TEST 0x14
#define NVME_OPC_ADMIN_NS_ATTACHMENT 0x15
#define NVME_OPC_ADMIN_KEEP_ALIVE 0x18
#define NVME_OPC_ADMIN_DIRECTIVE_SEND 0x19
#define NVME_OPC_ADMIN_DIRECTIVE_RECV 0x1A
#define NVME_OPC_ADMIN_VIRT_MNGMT 0x1C
#define NVME_OPC_ADMIN_NVME_MI_SEND 0x1D
#define NVME_OPC_ADMIN_NVME_MI_RECV 0x1E
#define NVME_OPC_ADMIN_CAP_MNGMT 0x20
#define NVME_OPC_ADMIN_LOCKDOWN 0x24
#define NVME_OPC_ADMIN_DB_BUF_CFG 0x7C
#define NVME_OPC_ADMIN_FABRICS_CMDS 0x7F
#define NVME_OPC_ADMIN_FORMAT_NVM 0x80
#define NVME_OPC_ADMIN_SEC_SEND 0x81
#define NVME_OPC_ADMIN_SEC_RECV 0x82
#define NVME_OPC_ADMIN_SANITIZE 0x84
#define NVME_OPC_ADMIN_GET_LBA_STATUS 0x86
// io command set
#define NVME_OPC_IO_FLUSH 0x0
#define NVME_OPC_IO_WRITE 0x1
#define NVME_OPC_IO_READ 0x2
#define NVME_OPC_IO_WRITE_UNCORRECTABLE 0x4
#define NVME_OPC_IO_COMPARE 0x5
#define NVME_OPC_IO_WRITE_ZEROES 0x8
#define NVME_OPC_IO_DATASET_MNGMT 0x9
#define NVME_OPC_IO_VERIFY 0xC
#define NVME_OPC_IO_RES_REG_BASE 0xD
#define NVME_OPC_IO_RES_REPORT_BASE 0xE
#define NVME_OPC_IO_RES_ACQ_BASE 0x11
#define NVME_OPC_IO_RES_REL_BASE 0x15
#define NVME_OPC_IO_COPY 0x19

// for now only support these two
#define NVME_CNS_NAMESPACE 0x00
#define NVME_CNS_CONTROLLER 0x01
#define NVME_CNS_NS_LIST 0x2

#define NVME_CMD_FLAGS_PRPS (0b00 << 14)
#define NVME_CMD_FLAGS_SGLS_CONTIG_PHYS (0b01 << 14)
#define NVME_CMD_FLAGS_SGLS_DESCRIPTOR (0b10 << 14)
#define NVME_CMD_FLAGS_FUSE_NORMAL 0b00
#define NVME_CMD_FLAGS_FUSE_FIRST 0b01
#define NVME_CMD_FLAGS_FUSE_SECOND 0b10

#define NVME_SQE_SIZE 64
#define NVME_CQE_SIZE 16
#define NVME_IDENTIFY_DS_SIZE 4096

#define NVME_STATUS_READY 0b1

#define NVME_CTRLER_TYPE_IO 0x1
#define NVME_CTRLER_TYPE_DISCOVERY 0x2
#define NVME_CTRLER_TYPE_ADMIN 0x3

#define NVME_PROPERTIES_MQES(prop) (prop->cap & 0xFFFFul)
#define NVME_PROPERTIES_DSTRD(prop) ((prop->cap >> 32) & 0xFul)

#define NVME_CQE_SUCCESSFUL(cqe) (!(cqe.status >> 1))

struct pci_device;
typedef struct pci_device pci_device;

// registers can be mandatory, optional or reserved
// important registers described in comments
struct nvme_controller_properties {
    uint64_t cap;       // ctrler capabilities
    uint32_t version;
    uint32_t intms;     // interrupt mask set
    uint32_t intmc;     // interrupt mask clear
    uint32_t cc;        // ctrler config
    uint32_t reserved_0;
    uint32_t csts;      // ctrler status
    uint32_t nssr;
    uint32_t aqa;       // admin queue attribs
    uint64_t asq;       // admin queue submission queue base addr
    uint64_t acq;       // admin queue completion queue base addr
    uint32_t cmbloc;
    uint32_t cmbsz;
    uint32_t bpinfo;
    uint32_t bprsel;
    uint64_t bpmbl;
    uint64_t cmbmsc;
    uint32_t cmbsts;
    uint32_t cmbebs;
    uint32_t cmbswtp;
    uint32_t nssd;
    uint32_t crto;
    // from here on irrelevant (optional, unused)
};

// submission commands layout: 16 DWORDS (64 bytes)
typedef union {
    struct {
        // first DWORD: same across all commands
        uint8_t opc;    // opcode [7] generic cmd, [6:2] function, [1:0] data transfer (direction)
        uint8_t flags;  // [1:0] FUSE, [7:6] PSDT (0 for PRPs)
        uint16_t cid;   // combined with qid, unique id

        uint32_t nsid;  // namespace id
        uint64_t cmd_specific_0;
        uint64_t mptr;
        uint64_t dptr_prp1; // dptrs page aligned
        uint64_t dptr_prp2;
        uint32_t cmd_specific_1[6];
    } common_command_format_entry;

    // admin commands
    struct {
        uint8_t opc;
        uint8_t flags;
        uint16_t cid;

        uint32_t reserved_0[2];
        uint32_t reserved_1[2];
        uint64_t prp1;
        uint64_t reserved_2;
        uint16_t qid;
        uint16_t qsize;
        uint16_t flags_2;
        uint16_t iv;
        uint32_t reserved_3[4];
    } create_io_cq;
    struct {
        uint8_t opc;
        uint8_t flags;
        uint16_t cid;

        uint32_t reserved_0;
        uint64_t reserved_1[2];
        uint64_t prp1;
        uint64_t reserved_2;
        uint16_t qid;
        uint16_t qsize;
        uint16_t flags_2;
        uint16_t cqid;
        uint16_t nvmesetid;
        uint16_t reserved_3;
        uint32_t reserved_4[3];
    } create_io_sq;
    struct {
        uint8_t opc;
        uint8_t flags;
        uint16_t cid;

        uint32_t reserved_0[3];
        uint64_t unused_0;
        uint64_t prp1;
        uint64_t prp2;
        uint8_t cns;
        uint8_t reserved_1;
        uint16_t cntid;
        uint16_t cns_specific_id;
        uint8_t reserved_2;
        uint8_t csi;
        uint32_t reserved_3[2];
        uint32_t uuid;
        uint32_t reserved_4;
    } identify;

    // io commands
    struct {
        uint8_t opc;
        uint8_t flags;
        uint16_t cid;

        uint32_t unused_0;
        uint64_t elbst_low;
        uint64_t mptr;
        uint64_t prp1;  // dptr: data to compare
        uint64_t prp2;
        uint64_t slba;  // starting lba
        uint32_t flags_2;
        uint32_t unused_1;
        uint32_t elbst_high;
        uint16_t elbatm;
        uint16_t elbat;
    } compare;
} nvme_scmd;

// completion commands layout
typedef struct {
    uint32_t cmd_specific_0[2];
    uint16_t sqhd;      // sq head ptr
    uint16_t sqid;      // sq id
    uint16_t cid;       // command id
    uint16_t status;    // [0] phase bit
} nvme_ccmd;

struct nvme_submission_queue {
    void *_metadata_pointer_unaligned;
    volatile nvme_scmd *data;
    volatile uint32_t *sqt; // db
    size_t tail;
    // each cqe informs us about the new sqhd for the corresponding sqe that was consumed for this cqe
    size_t head;
    size_t free_entries;    // full if free_entries == entries - 1, dec for each submitted command, and inc for each completed one
    uint16_t cid_counter;   // manually increase cid for each submitted command
};

struct nvme_completion_queue {
    void *_metadata_pointer_unaligned;
    volatile nvme_ccmd *data;
    volatile uint32_t *cqh; // db
    size_t head;
    uint8_t phase;  // swaps between 0 and 1 every wraparound; zero-initialized
};

struct nvme_queue_ctx {
    // store the pointers to where we allocated from, so we can free() them accordingly
    // don't read from doorbells
    // optimization note: implement multiple write and poll aswell as advanced free entry tracking
    struct nvme_submission_queue sq;
    struct nvme_completion_queue cq;
    size_t entries;
    uint16_t queue_id;
};

// controller data structures

union nvme_identify_ds {
    struct {
        // ctrler caps and features
        uint16_t vid;
        uint16_t ssvid;
        char sn[20];
        char mn[40];
        char fr[8];
        uint8_t rab;
        uint8_t ieee[3];
        uint8_t cmic;
        uint8_t mdts;
        uint16_t cntlid;
        uint32_t ver;
        uint32_t rtd3r;
        uint32_t rtd3e;
        uint32_t oaes;
        uint32_t ctratt;
        uint16_t rrls;
        uint8_t reserved_0[9];
        uint8_t cntrltype;
        uint8_t fguid[16];
        uint16_t crdt1;
        uint16_t crdt2;
        uint16_t crdt3;
        uint8_t reserved_1[105];
        uint8_t reserved_2[13];
        uint8_t nvmsr;
        uint8_t vwci;
        uint8_t mec;

        // admin cmd set attribs and opt ctrler caps
        uint16_t oacs;
        uint8_t acl;
        uint8_t aerl;
        uint8_t frmw;
        uint8_t lpa;
        uint8_t elpe;
        uint8_t npss;
        uint8_t avscc;
        uint8_t apsta;
        uint16_t wctemp;
        uint16_t cctemp;
        uint16_t mtfa;
        uint32_t hmpre;
        uint32_t hmmin;
        uint64_t tnvmcap[2];
        uint64_t unvmcap[2];
        uint32_t rpmbs;
        uint16_t edstt;
        uint8_t dsto;
        uint8_t fwug;
        uint16_t kas;
        uint16_t hctma;
        uint16_t mntmt;
        uint16_t mxtmt;
        uint32_t sanicap;
        uint32_t hmminds;
        uint16_t hmmaxd;
        uint16_t nsetidmax;
        uint16_t endgidmax;
        uint8_t anatt;
        uint8_t anacap;
        uint32_t anagrpmax;
        uint32_t nanagrpid;
        uint32_t pels;
        uint16_t domain_id;
        uint16_t reserved_3;
        uint64_t megcap[2];
        uint8_t reserved_4[128];

        // NVM cmd set attribs
        uint8_t sqes;
        uint8_t cqes;
        uint16_t maxcmd;
        uint32_t nn;    // number of namespaces
        uint16_t oncs;
        uint32_t fuses[3];
        uint8_t fna;
        uint8_t vwc;
        uint16_t awun;
        uint16_t awupf;
        uint8_t icscscc;
        uint8_t nwpc;
        uint16_t acwu;
        uint16_t cpy_descr_fmts_supported;
        uint32_t sgls;
        uint32_t mnan;
        uint8_t maxdna[6];
        uint32_t maxcna;
        uint8_t reserved_5[204];
        char subnqn[256];
        uint8_t reserved_6[768];

        // fabric (not implemented)
        uint8_t fabric_specific[256];

        // power state decriptors (32 bytes)
        struct power_state_descriptor {
            uint16_t mp;
            uint8_t reserved_0;
            uint8_t mxps_nops;
            uint32_t enlat;
            uint32_t exlat;
            uint8_t rrt;
            uint8_t rrl;
            uint8_t rwt;
            uint8_t rwl;
            uint16_t idlp;
            uint8_t ips;    // ips << 6
            uint8_t reserved_1;
            uint16_t actp;
            uint8_t apw_aps;
            uint8_t reserved_2[9];
        } power_state_descriptors[32];

        // vendor specific
        uint8_t vendor_specific[1024];
    } ctrler;

    struct ns {
        uint8_t u[4096];
    } ns;
};

// single pci device controller: [TODO], the driver should be able to handle n amount
// of them and have a generic implementation to the gdi over the pci_device layer
struct nvme_controller {
    volatile struct nvme_controller_properties *properties; // pci(e) bar0 registers
    struct nvme_queue_ctx aq;   // admin queue for this controller (each controller type has one)
    union nvme_identify_ds *ctrler_identify;
    size_t mdts;    // max data transfer size (bytes), ~0ul if no max size
    uint16_t cntlid;
};

void init_nvme_controller(pci_device *dev);
bool nvme_queue_submit_single_cmd(struct nvme_queue_ctx *queue, nvme_scmd *cmd);