/*
 * resource_table.c - Global RemoteProc Resource Table for XuanTie E907 (T527)
 *
 * Configurable via macros defined in include/resource_table.h or Makefile -D flags.
 */

#include "include/resource_table.h"

#ifdef CONFIG_RPROC_RPMSG

__attribute__((used, section(".resource_table"), aligned(4)))
const struct rpmsg_resource_table global_resource_table = {
    .ver        = 1,
    .num        = 2,
    .reserved   = {0, 0},
    .offset     = {
        offsetof(struct rpmsg_resource_table, trace),
        offsetof(struct rpmsg_resource_table, vdev),
    },
    .trace = {
        .type     = RSC_TRACE,
        .da       = CONFIG_RPROC_TRACE0_DA,
        .len      = CONFIG_RPROC_TRACE0_LEN,
        .reserved = 0,
        .name     = CONFIG_RPROC_TRACE0_NAME,
    },
    .vdev = {
        .type          = RSC_VDEV,
        .id            = VIRTIO_ID_RPMSG,
        .notifyid      = 0,
        .dfeatures     = (1 << VIRTIO_RPMSG_F_NS),
        .gfeatures     = 0,
        .config_len    = 0,
        .status        = 0,
        .num_of_vrings = 2,
        .reserved      = {0, 0},
        .vring = {
            {
                .da       = 0, /* Host allocates dynamic buffer/da */
                .align    = VRING_ALIGN,
                .num      = VRING_NUM_DESCS,
                .notifyid = 0,
                .reserved = 0,
            },
            {
                .da       = 0, /* Host allocates dynamic buffer/da */
                .align    = VRING_ALIGN,
                .num      = VRING_NUM_DESCS,
                .notifyid = 1,
                .reserved = 0,
            },
        },
    },
};

#else

__attribute__((used, section(".resource_table"), aligned(4)))
const struct standard_resource_table global_resource_table = {
    .ver        = 1,
    .num        = 1,
    .reserved   = {0, 0},
    .offset     = {
        offsetof(struct standard_resource_table, trace),
    },
    .trace = {
        .type     = RSC_TRACE,
        .da       = CONFIG_RPROC_TRACE0_DA,
        .len      = CONFIG_RPROC_TRACE0_LEN,
        .reserved = 0,
        .name     = CONFIG_RPROC_TRACE0_NAME,
    },
};

#endif

