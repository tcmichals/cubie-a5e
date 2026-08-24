/*
 * resource_table.c - RemoteProc resource table for Allwinner XuanTie E907
 *
 * Declares one RSC_TRACE buffer at 0x00029000 in Shared SRAM C so the ARM host
 * can read RISC-V printk and telemetry strings from debugfs
 * (/sys/kernel/debug/remoteproc/remoteproc0/trace0) without colliding with
 * the binary telemetry block at 0x00028000.
 */

#include <stdint.h>
#include <stddef.h>

#define TRACE_BUF_DA            0x00029000  /* Dedicated trace buffer in Shared SRAM C */
#define TRACE_BUF_LEN           0x1000      /* 4 KB trace log window */

/* Resource Types */
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

/* Resource Table with 1 entry: Trace Buffer */
struct cubie_resource_table {
    /* header */
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    /* offsets to each entry */
    uint32_t offset[1];
    /* entry 0: trace */
    struct fw_rsc_trace trace;
} __attribute__((packed));

/* Place the table in .resource_table ELF section with used attribute */
__attribute__((used, section(".resource_table"), aligned(4)))
const struct cubie_resource_table resource_table = {
    .ver        = 1,
    .num        = 1,
    .reserved   = {0, 0},
    .offset     = {
        offsetof(struct cubie_resource_table, trace),
    },
    .trace = {
        .type     = RSC_TRACE,
        .da       = TRACE_BUF_DA,
        .len      = TRACE_BUF_LEN,
        .reserved = 0,
        .name     = "trace0",
    },
};

/* ---- Simple trace printf into the trace buffer ---- */
static char *trace_buf = (char *)TRACE_BUF_DA;
static int   trace_pos = 0;

void trace_init(void) {
    for (int i = 0; i < TRACE_BUF_LEN; i++) {
        trace_buf[i] = '\0';
    }
    trace_pos = 0;
}

void trace_puts(const char *s) {
    while (*s) {
        if (trace_pos >= (int)(TRACE_BUF_LEN - 1)) {
            trace_pos = 0;
        }
        trace_buf[trace_pos++] = *s++;
    }
    trace_buf[trace_pos] = '\0';
}
