/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_FW_TRACE_H_
#define _RWNX_FW_TRACE_H_

#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "lmac_types.h"

#define FW_LOG_SIZE (10240)

struct rwnx_fw_log_buf {
	u8_l *data;
	u8_l *start;
	u8_l *end;
	u8_l *dataend;
	u32_l size;
};

struct rwnx_fw_log {
	struct rwnx_fw_log_buf buf;
	//
	spinlock_t lock;
};

int rwnx_fw_log_init(struct rwnx_fw_log *fw_log);
void rwnx_fw_log_deinit(struct rwnx_fw_log *fw_log);
#endif /* _RWNX_FW_TRACE_H_ */
