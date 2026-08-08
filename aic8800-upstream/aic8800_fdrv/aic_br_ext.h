/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bridge function definitions
 *
 ****************************************************************************
 */

#ifndef _AIC_BR_EXT_H_
#define _AIC_BR_EXT_H_

#define CL_IPV6_PASS    1
#define MACADDRLEN      6
#define WLAN_ETHHDR_LEN 14

#define NAT25_HASH_BITS   4
#define NAT25_HASH_SIZE   BIT(NAT25_HASH_BITS)
#define NAT25_AGEING_TIME 300

#define NDEV_FMT            "%s"
#define NDEV_ARG(ndev)      ((ndev)->name)
#define ADPT_FMT            "%s"
// #define ADPT_ARG(adapter) (adapter->pnetdev ? adapter->pnetdev->name : NULL)
#define FUNC_NDEV_FMT       "%s(%s)"
#define FUNC_NDEV_ARG(ndev) (__func__, (ndev)->name)
#define FUNC_ADPT_FMT       "%s(%s)"
// #define FUNC_ADPT_ARG(adapter) __func__, (adapter->pnetdev ?
// adapter->pnetdev->name : NULL)
#define MAC_FMT             "%02x:%02x:%02x:%02x:%02x:%02x"

#ifdef CL_IPV6_PASS
#define MAX_NETWORK_ADDR_LEN 17
#else
#define MAX_NETWORK_ADDR_LEN 11
#endif

struct nat25_network_db_entry {
	struct nat25_network_db_entry *next_hash;
	struct nat25_network_db_entry **pprev_hash;
	atomic_t use_count;
	unsigned char mac_addr[6];
	unsigned long ageing_timer;
	unsigned char network_addr[MAX_NETWORK_ADDR_LEN];
};

enum NAT25_METHOD {
	NAT25_MIN,
	NAT25_CHECK,
	NAT25_INSERT,
	NAT25_LOOKUP,
	NAT25_PARSE,
	NAT25_MAX
};

struct br_ext_info {
	unsigned int nat25_disable;
	unsigned int macclone_enable;
	unsigned int dhcp_bcst_disable;
	int add_pppoe_tag; /* 1: Add PPPoE relay-SID, 0: disable */
	unsigned char nat25_dmz_mac[MACADDRLEN];
	unsigned int nat25sc_disable;
};

void nat25_db_cleanup(struct rwnx_vif *vif);
int nat25_handle_frame(struct rwnx_vif *vif, struct sk_buff *skb);
int aic_br_client_tx(struct rwnx_vif *vif, struct sk_buff **pskb);
void netdev_br_init(struct net_device *netdev);

#endif /* _AIC_BR_EXT_H_ */
