/*
 * resource_table.c - RemoteProc resource table for Allwinner T527 XuanTie E906/E907
 *
 * This ELF section is parsed by the Linux remoteproc core when it loads
 * firmware.elf.  It declares:
 *   - One RSC_TRACE buffer so the ARM host can read RISC-V printk strings
 *     from debugfs (/sys/kernel/debug/remoteproc/remoteproc0/trace0).
 *   - One RSC_VDEV  (virtio rpmsg device) with two vrings so the RPMsg
 *     transport works over shared SRAM C.
 *
 * Place this file in SRAM C by adding it to the Makefile SRCS list.
 * The linker script must place the ".resource_table" section at a fixed,
 * known address inside the SRAM C window.
 */

#include <stdint.h>
#include <stddef.h>


/* ---- constants that must match the Linux driver ---- */
#define RPMSG_IPU_C0_FEATURES   1
#define VRING0_DA               0x00078000  /* TX vring (RISC-V→ARM), in SRAM C */
#define VRING1_DA               0x00079000  /* RX vring (ARM→RISC-V), in SRAM C */
#define VRING_ALIGN             0x1000
#define VRING_SIZE              16          /* number of buffers (power of 2) */
#define TRACE_BUF_DA            0x0007A000  /* trace buffer in SRAM C */
#define TRACE_BUF_LEN           0x1000      /* 4 KB trace log window */

/* ---- packed resource table layout ---- */

/* virtio IDs */
#define VIRTIO_ID_RPMSG  7

/* resource types */
#define RSC_CARVEOUT  0
#define RSC_DEVMEM    1
#define RSC_TRACE     2
#define RSC_VDEV      3

struct fw_rsc_trace {
    uint32_t type;
    uint32_t da;
    uint32_t len;
    uint32_t reserved;
    char     name[32];
} __attribute__((packed));

struct fw_rsc_vdev_vring {
    uint32_t da;
    uint32_t align;
    uint32_t num;
    uint32_t notifyid;
    uint32_t pa;
} __attribute__((packed));

struct fw_rsc_vdev {
    uint32_t type;
    uint32_t id;
    uint32_t notifyid;
    uint32_t dfeatures;
    uint32_t gfeatures;
    uint32_t config_len;
    uint8_t  status;
    uint8_t  num_of_vrings;
    uint8_t  reserved[2];
    struct   fw_rsc_vdev_vring vring[2];
} __attribute__((packed));

/* The full table with 2 entries: trace + vdev */
struct cubie_resource_table {
    /* header */
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    /* offsets to each entry (relative to start of this struct) */
    uint32_t offset[2];
    /* entry 0: trace */
    struct fw_rsc_trace trace;
    /* entry 1: vdev */
    struct fw_rsc_vdev  vdev;
} __attribute__((packed));

/* Place the table in the .resource_table ELF section. */
__attribute__((section(".resource_table")))
const struct cubie_resource_table resource_table = {
    .ver        = 1,
    .num        = 2,
    .reserved   = {0, 0},
    /* offsets: skip header (5 uint32s) = 20 bytes */
    .offset     = {
        offsetof(struct cubie_resource_table, trace),
        offsetof(struct cubie_resource_table, vdev),
    },

    /* ---- Entry 0: trace buffer ---- */
    .trace = {
        .type     = RSC_TRACE,
        .da       = TRACE_BUF_DA,
        .len      = TRACE_BUF_LEN,
        .reserved = 0,
        .name     = "e906-trace",
    },

    /* ---- Entry 1: virtio rpmsg vdev ---- */
    .vdev = {
        .type          = RSC_VDEV,
        .id            = VIRTIO_ID_RPMSG,
        .notifyid      = 0,
        .dfeatures     = RPMSG_IPU_C0_FEATURES,
        .gfeatures     = 0,
        .config_len    = 0,
        .status        = 0,
        .num_of_vrings = 2,
        .reserved      = {0, 0},
        .vring = {
            /* vring[0]: RISC-V → ARM (TX from co-processor side) */
            {
                .da       = VRING0_DA,
                .align    = VRING_ALIGN,
                .num      = VRING_SIZE,
                .notifyid = 0,
                .pa       = 0,
            },
            /* vring[1]: ARM → RISC-V (RX on co-processor side) */
            {
                .da       = VRING1_DA,
                .align    = VRING_ALIGN,
                .num      = VRING_SIZE,
                .notifyid = 1,
                .pa       = 0,
            },
        },
    },
};

/* ---- Simple trace printf into the trace buffer ---- */
static char *trace_buf     = (char *)TRACE_BUF_DA;
static int   trace_pos     = 0;

void trace_puts(const char *s) {
    while (*s && trace_pos < (int)(TRACE_BUF_LEN - 1))
        trace_buf[trace_pos++] = *s++;
    trace_buf[trace_pos] = '\0';
}
