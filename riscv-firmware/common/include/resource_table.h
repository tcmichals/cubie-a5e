#ifndef RESOURCE_TABLE_H
#define RESOURCE_TABLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standard Linux RemoteProc Resource Types
 */
#define RSC_CARVEOUT    0
#define RSC_DEVMEM      1
#define RSC_TRACE       2
#define RSC_VDEV        3

/*
 * VirtIO RemoteProc Constants
 */
#define VIRTIO_ID_RPMSG                7
#define VIRTIO_RPMSG_F_NS              0   /* Bit 0: Name Service Announcement */

#define VIRTIO_CONFIG_S_ACKNOWLEDGE    1
#define VIRTIO_CONFIG_S_DRIVER         2
#define VIRTIO_CONFIG_S_DRIVER_OK      4
#define VIRTIO_CONFIG_S_FEATURES_OK    8
#define VIRTIO_CONFIG_S_NEEDS_RESET    64
#define VIRTIO_CONFIG_S_FAILED         128

#define VRING_ALIGN                    4096
#define VRING_NUM_DESCS                16

/*
 * Standard RPMsg Protocol Constants
 */
#define RPMSG_NS_ADDR                  53
#define RPMSG_NS_CREATE                0
#define RPMSG_NS_DESTROY               1
#define RPMSG_PING_EPT_ADDR            1024
#define RPMSG_BUFFER_SIZE              512

#ifndef CONFIG_RPROC_TRACE0_LEN
#define CONFIG_RPROC_TRACE0_LEN     4096UL       /* 4 KB Trace Buffer */
#endif

#ifndef CONFIG_RPROC_TRACE0_NAME
#define CONFIG_RPROC_TRACE0_NAME    "trace0"
#endif

extern char g_rproc_trace_buffer[CONFIG_RPROC_TRACE0_LEN];
extern char __trace_start[];
extern char __trace_end[];

/*
 * RemoteProc Resource Descriptors
 */
struct fw_rsc_hdr {
    uint32_t type;
    uint8_t  data[];
} __attribute__((packed));

struct fw_rsc_trace {
    uint32_t type;
    uint32_t da;
    uint32_t len;
    uint32_t reserved;
    char     name[32];
} __attribute__((packed));

struct fw_rsc_carveout {
    uint32_t type;
    uint32_t da;
    uint32_t pa;
    uint32_t len;
    uint32_t flags;
    uint32_t reserved;
    char     name[32];
} __attribute__((packed));

struct fw_rsc_vdev_vring {
    uint32_t da;       /* device address (0 = auto-allocated by host) */
    uint32_t align;    /* min alignment for vring */
    uint32_t num;      /* number of descriptors */
    uint32_t notifyid; /* vring notification id */
    uint32_t reserved;
} __attribute__((packed));

struct fw_rsc_vdev {
    uint32_t type;
    uint32_t id;         /* virtio device id (e.g. VIRTIO_ID_RPMSG = 7) */
    uint32_t notifyid;   /* notify id */
    uint32_t dfeatures;  /* host/device features */
    uint32_t gfeatures;  /* guest features */
    uint32_t config_len; /* custom config length */
    uint8_t  status;     /* virtio status */
    uint8_t  num_of_vrings;
    uint8_t  reserved[2];
    struct fw_rsc_vdev_vring vring[2];
} __attribute__((packed));

/*
 * Standard Single-Trace Resource Table Structure (testBasic, testTrace, testCrash, testPing)
 */
struct standard_resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1];
    struct fw_rsc_trace trace;
} __attribute__((packed));

/*
 * Standard Trace + VirtIO RPMsg Resource Table Structure (testPingRpmsg)
 */
struct rpmsg_resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[2];
    struct fw_rsc_trace trace;
    struct fw_rsc_vdev  vdev;
} __attribute__((packed));

/*
 * Standard RPMsg Packet Header & Name Service
 */
struct rpmsg_hdr {
    uint32_t src;
    uint32_t dst;
    uint32_t reserved;
    uint16_t len;
    uint16_t flags;
    uint8_t  data[];
} __attribute__((packed));

struct rpmsg_ns_msg {
    char     name[32];
    uint32_t addr;
    uint32_t flags;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* RESOURCE_TABLE_H */

