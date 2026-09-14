#ifndef RESOURCE_TABLE_H
#define RESOURCE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Remoteproc Resource Types */
#define RSC_CARVEOUT      0
#define RSC_DEVMEM        1
#define RSC_TRACE         2
#define RSC_VDEV          3

#define TRACE_BUFFER_SIZE 4096

/* Standard Remoteproc Resource Table Header */
struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1]; /* Offsets to tracking resources */
} __attribute__((packed));

/* Trace buffer resource entry */
struct fw_rsc_trace {
    uint32_t type;
    uint32_t da;        /* Device Address where trace buffer resides */
    uint32_t len;       /* Length in bytes */
    uint32_t reserved;
    uint8_t  name[32];  /* Resource name, e.g. "trace0" */
} __attribute__((packed));

/* Resource table for testBasic (Trace0 only) */
struct basic_resource_table {
    struct resource_table base;
    struct fw_rsc_trace trace;
} __attribute__((packed));

/* Global trace buffer buffer exported to Linux */
extern char g_dsp_trace_buffer[TRACE_BUFFER_SIZE];
extern const struct basic_resource_table global_dsp_resource_table;

/* Helper logging function for DSP apps */
void dsp_trace_init(void);
void dsp_trace_puts(const char *str);
void dsp_trace_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* RESOURCE_TABLE_H */
