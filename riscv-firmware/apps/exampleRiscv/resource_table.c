/*
 * resource_table.c - RemoteProc Resource Table for XuanTie E907
 * Exports a trace buffer in Linux-reserved normal DDR.
 */

#include <stdint.h>
#include <stddef.h>

#include "include/memory_map.h"

#define TRACE_BUF_DA            0x4E010000  /* DDR Carveout */
#define TRACE_BUF_LEN           4096        /* 4 KB Trace Buffer */

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

struct cubie_resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1];
    struct fw_rsc_trace trace;
} __attribute__((packed));

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

static char *trace_buf = (char *)(uintptr_t)TRACE_BUF_DA;
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
