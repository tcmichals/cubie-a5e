// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2024 AIC semiconductor.
 *
 * @file usb_host.c
 * @brief USB host interface and descriptor handling
 */

#include "usb_host.h"
#include "rwnx_tx.h"
#include "rwnx_platform.h"
#include "aicwf_debug.h"

void aicwf_usb_host_init(struct usb_host_env_tag *env,
			 void *cb,
			 void *shared_env_ptr,
			 void *pthis)
{
	memset(env, 0, sizeof(struct usb_host_env_tag));
	env->pthis = pthis;
}

volatile struct txdesc_host *aicwf_usb_host_txdesc_get(struct usb_host_env_tag *env, const int queue_idx)
{
	volatile struct txdesc_host *txdesc_free = NULL;
	uint32_t used_idx = env->txdesc_used_idx[queue_idx];
	uint32_t free_idx = env->txdesc_free_idx[queue_idx];

	if (free_idx != (used_idx + USB_TXDESC_CNT)) {
		/* free descriptor available */
	} else {
		txdesc_free = NULL;
	}

	return txdesc_free;
}

void aicwf_usb_host_txdesc_push(struct usb_host_env_tag *env, const int queue_idx, const uint64_t host_id)
{
	env->tx_host_id[queue_idx][env->txdesc_free_idx[queue_idx] % USB_TXDESC_CNT] = host_id;

	env->txdesc_free_idx[queue_idx]++;
	if (env->txdesc_free_idx[queue_idx] == 0x80000000)
		env->txdesc_free_idx[queue_idx] = 0;
}

void aicwf_usb_host_tx_cfm_handler(struct usb_host_env_tag *env, u32 *data)
{
	u32 queue_idx = 0;
	struct sk_buff *skb = NULL;
	struct rwnx_txhdr *txhdr;
	uint32_t used_idx = data[1];
	uint64_t host_id = env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT];

	env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT] = 0;

	if (host_id == 0) {
		env->txdesc_used_idx[queue_idx] = used_idx;
		AICWFDBG(LOGERROR, "ERROR: No more confirmations\r\n");
		return;
	}

	skb = (struct sk_buff *)(uintptr_t)host_id;
	txhdr = (struct rwnx_txhdr *)skb->data;
	txhdr->hw_hdr.cfm.status = (union rwnx_hw_txstatus)data[0];

	if (rwnx_txdatacfm(env->pthis, (void *)(uintptr_t)host_id) != 0) {
		env->txdesc_used_idx[queue_idx] = used_idx;
		env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT] = host_id;
		AICWFDBG(LOGERROR, "ERROR: rwnx_txdatacfm\r\n");
	}
}

int aicwf_rwnx_usb_platform_init(struct aic_usb_dev *usbdev)
{
	struct rwnx_plat *rwnx_plat = NULL;
	void *drvdata;
	int ret = -ENODEV;

	rwnx_plat = kzalloc(sizeof(struct rwnx_plat), GFP_KERNEL);
	if (!rwnx_plat)
		return -ENOMEM;

	rwnx_plat->usbdev = usbdev;
	ret = rwnx_platform_init(rwnx_plat, &drvdata);

	return ret;
}

