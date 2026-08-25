#include <stdint.h>

#define RSC_CARVEOUT    0
#define RSC_DEVTREE     1
#define RSC_TRACE       2
#define RSC_VDEV        3

struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1];
} __attribute__((packed));

struct fw_rsc_trace {
    uint32_t type;
    uint32_t da;
    uint32_t len;
    uint32_t reserved;
    uint8_t name[32];
} __attribute__((packed));

struct my_resource_table {
    struct resource_table base;
    struct fw_rsc_trace trace;
} __attribute__((packed));

__attribute__((section(".resource_table"), used))
const struct my_resource_table rproc_resource_table = {
    .base = {
        .ver = 1,
        .num = 1,
        .reserved = {0, 0},
        .offset = {
            sizeof(struct resource_table),
        },
    },
    .trace = {
        .type = RSC_TRACE,
        .da = 0x07138100, /* IPC_TRACE_BUFFER_OFFSET in SRAM C */
        .len = 0x8000,    /* 32 KB trace buffer */
        .reserved = 0,
        .name = "trace:ioprocessor",
    },
};
