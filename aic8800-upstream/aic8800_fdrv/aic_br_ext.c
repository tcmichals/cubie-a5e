// SPDX-License-Identifier: GPL-2.0
/*
 *****************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bridge function definitions
 *
 ****************************************************************************
 */

#define _AIC_BR_EXT_C_

#ifdef __KERNEL__
#include "rwnx_defs.h"
#include <linux/atalk.h>
#include <linux/if_arp.h>
#include <linux/if_pppox.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <net/ipx.h>
#endif

#ifdef CL_IPV6_PASS
#ifdef __KERNEL__
#include <linux/icmpv6.h>
#include <linux/ipv6.h>
#include <net/ndisc.h>
#include <net/checksum.h>
#endif
#endif

#ifdef CONFIG_AIC8800_BR_SUPPORT

/* #define BR_SUPPORT_DEBUG */

#define NAT25_IPV4  01
#define NAT25_IPV6  02
#define NAT25_IPX   03
#define NAT25_APPLE 04
#define NAT25_PPPOE 05

#define RTL_RELAY_TAG_LEN (ETH_ALEN)
#define TAG_HDR_LEN       4

#define MAGIC_CODE      0x8186
#define MAGIC_CODE_LEN  2
#define WAIT_TIME_PPPOE 5 /* waiting time for pppoe server in sec */

/*-----------------------------------------------------------------
 * How database records network address:
 *		   0    1    2    3    4    5    6    7    8    9   10
 *		|----|----|----|----|----|----|----|----|----|----|----|
 * IPv4  |type|                             |      IP addr      |
 * IPX   |type|      Net addr     |          Node addr          |
 * IPX   |type|      Net addr     |Sckt addr|
 * Apple |type| Network |node|
 * PPPoE |type|   SID   |           AC MAC            |
 *-----------------------------------------------------------------
 */

/* Find a tag in pppoe frame and return the pointer */
static inline unsigned char *__nat25_find_pppoe_tag(struct pppoe_hdr *ph,
						    unsigned short type)
{
	unsigned char *cur_ptr, *start_ptr;
	unsigned short tag_len, tag_type;

	cur_ptr = (unsigned char *)ph->tag;
	start_ptr = cur_ptr;
	while ((cur_ptr - start_ptr) < ntohs(ph->length)) {
		/* prevent un-alignment access */
		tag_type = (unsigned short)((cur_ptr[0] << 8) + cur_ptr[1]);
		tag_len = (unsigned short)((cur_ptr[2] << 8) + cur_ptr[3]);
		if (tag_type == type)
			return cur_ptr;
		cur_ptr = cur_ptr + TAG_HDR_LEN + tag_len;
	}
	return 0;
}

static inline int __nat25_add_pppoe_tag(struct sk_buff *skb,
					struct pppoe_tag *tag)
{
	struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
	int data_len;

	data_len = tag->tag_len + TAG_HDR_LEN;
	if (skb_tailroom(skb) < data_len) {
		pr_err("skb_tailroom() failed in add SID tag!\n");
		return -1;
	}

	skb_put(skb, data_len);
	/* have a room for new tag */
	memmove(((unsigned char *)ph->tag + data_len), (unsigned char *)ph->tag,
		ntohs(ph->length));
	ph->length = htons(ntohs(ph->length) + data_len);
	memcpy((unsigned char *)ph->tag, tag, data_len);
	return data_len;
}

static int skb_pull_and_merge(struct sk_buff *skb, unsigned char *src, int len)
{
	int tail_len;
	unsigned long end, tail;

	if ((src + len) > skb_tail_pointer(skb) || skb->len < len)
		return -1;

	tail = (unsigned long)skb_tail_pointer(skb);
	end = (unsigned long)src + len;
	if (tail < end)
		return -1;

	tail_len = (int)(tail - end);
	if (tail_len > 0)
		memmove(src, src + len, tail_len);

	skb_trim(skb, skb->len - len);
	return 0;
}

static inline unsigned long __nat25_timeout(struct rwnx_vif *vif)
{
	unsigned long timeout;

	timeout = jiffies - NAT25_AGEING_TIME * HZ;

	return timeout;
}

static inline int __nat25_has_expired(struct rwnx_vif *vif,
				      struct nat25_network_db_entry *fdb)
{
	if (time_before_eq(fdb->ageing_timer, __nat25_timeout(vif)))
		return 1;

	return 0;
}

static inline void
__nat25_generate_ipv4_network_addr(unsigned char *network_addr,
				   unsigned int *ip_addr)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_IPV4;
	memcpy(network_addr + 7, (unsigned char *)ip_addr, 4);
}

static inline void
__nat25_generate_ipx_network_addr_with_node(unsigned char *network_addr,
					    unsigned int *ipx_net_addr,
					    unsigned char *ipx_node_addr)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_IPX;
	memcpy(network_addr + 1, (unsigned char *)ipx_net_addr, 4);
	memcpy(network_addr + 5, ipx_node_addr, 6);
}

static inline void
__nat25_generate_ipx_network_addr_with_socket(unsigned char *network_addr,
					      unsigned int *ipx_net_addr,
					      unsigned short *ipx_socket_addr)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_IPX;
	memcpy(network_addr + 1, (unsigned char *)ipx_net_addr, 4);
	memcpy(network_addr + 5, (unsigned char *)ipx_socket_addr, 2);
}

static inline void __nat25_generate_apple_network_addr(unsigned char *network_addr,
						       unsigned short *network,
						       unsigned char *node)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_APPLE;
	memcpy(network_addr + 1, (unsigned char *)network, 2);
	network_addr[3] = *node;
}

static inline void
__nat25_generate_pppoe_network_addr(unsigned char *network_addr,
				    unsigned char *ac_mac, unsigned short *sid)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_PPPOE;
	memcpy(network_addr + 1, (unsigned char *)sid, 2);
	memcpy(network_addr + 3, (unsigned char *)ac_mac, 6);
}

#ifdef CL_IPV6_PASS
static void __nat25_generate_ipv6_network_addr(unsigned char *network_addr,
					       unsigned int *ip_addr)
{
	memset(network_addr, 0, MAX_NETWORK_ADDR_LEN);

	network_addr[0] = NAT25_IPV6;
	memcpy(network_addr + 1, (unsigned char *)ip_addr, 16);
}

static unsigned char *scan_tlv(unsigned char *data, int len, unsigned char tag,
			       unsigned char len8b)
{
	while (len > 0) {
		if (*data == tag && *(data + 1) == len8b && len >= len8b * 8)
			return data + 2;

		len -= (*(data + 1)) * 8;
		data += (*(data + 1)) * 8;
	}
	return NULL;
}

static int update_nd_link_layer_addr(unsigned char *data, int len,
				     unsigned char *replace_mac)
{
	struct icmp6hdr *icmphdr = (struct icmp6hdr *)data;
	unsigned char *mac;

	if (icmphdr->icmp6_type == NDISC_ROUTER_SOLICITATION) {
		if (len >= 8) {
			mac = scan_tlv(&data[8], len - 8, 1, 1);
			if (mac) {
				pr_info("Router Solicitation, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					replace_mac[0], replace_mac[1], replace_mac[2],
					replace_mac[3], replace_mac[4], replace_mac[5]);
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_ROUTER_ADVERTISEMENT) {
		if (len >= 16) {
			mac = scan_tlv(&data[16], len - 16, 1, 1);
			if (mac) {
				pr_info("Router Advertisement, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					replace_mac[0], replace_mac[1], replace_mac[2],
					replace_mac[3], replace_mac[4], replace_mac[5]);
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len - 24, 1, 1);
			if (mac) {
				pr_info("Neighbor Solicitation, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					replace_mac[0], replace_mac[1], replace_mac[2],
					replace_mac[3], replace_mac[4], replace_mac[5]);
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len - 24, 2, 1);
			if (mac) {
				pr_info("Neighbor Advertisement, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					replace_mac[0], replace_mac[1], replace_mac[2],
					replace_mac[3], replace_mac[4], replace_mac[5]);
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_REDIRECT) {
		if (len >= 40) {
			mac = scan_tlv(&data[40], len - 40, 2, 1);
			if (mac) {
				pr_info("Redirect,  replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					replace_mac[0], replace_mac[1], replace_mac[2],
					replace_mac[3], replace_mac[4], replace_mac[5]);
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	}
	return 0;
}

#ifdef SUPPORT_RX_UNI2MCAST
static void convert_ipv6_mac_to_mc(struct sk_buff *skb)
{
	struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + ETH_HLEN);
	unsigned char *dst_mac = skb->data;

	/*modified by qinjunjie,ipv6 multicast address ix 0x33-33-xx-xx-xx-xx */
	dst_mac[0] = 0x33;
	dst_mac[1] = 0x33;
	memcpy(&dst_mac[2], &iph->daddr.s6_addr32[3], 4);
#if defined(__LINUX_2_6__)
	/*modified by qinjunjie,warning:should not remove next line */
	skb->pkt_type = PACKET_MULTICAST;
#endif
}
#endif /* CL_IPV6_PASS */
#endif /* SUPPORT_RX_UNI2MCAST */

static inline int __nat25_network_hash(unsigned char *network_addr)
{
	if (network_addr[0] == NAT25_IPV4) {
		unsigned long x;

		x = network_addr[7] ^ network_addr[8] ^ network_addr[9] ^ network_addr[10];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (network_addr[0] == NAT25_IPX) {
		unsigned long x;

		x = network_addr[1] ^ network_addr[2] ^ network_addr[3] ^ network_addr[4] ^
			network_addr[5] ^ network_addr[6] ^ network_addr[7] ^ network_addr[8] ^
			network_addr[9] ^ network_addr[10];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (network_addr[0] == NAT25_APPLE) {
		unsigned long x;

		x = network_addr[1] ^ network_addr[2] ^ network_addr[3];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (network_addr[0] == NAT25_PPPOE) {
		unsigned long x;

		x = network_addr[0] ^ network_addr[1] ^ network_addr[2] ^ network_addr[3] ^
			network_addr[4] ^ network_addr[5] ^ network_addr[6] ^ network_addr[7] ^
			network_addr[8];

		return x & (NAT25_HASH_SIZE - 1);
#ifdef CL_IPV6_PASS
	} else if (network_addr[0] == NAT25_IPV6) {
		unsigned long x;

		x = network_addr[1] ^ network_addr[2] ^ network_addr[3] ^ network_addr[4] ^
			network_addr[5] ^ network_addr[6] ^ network_addr[7] ^ network_addr[8] ^
			network_addr[9] ^ network_addr[10] ^ network_addr[11] ^
			network_addr[12] ^ network_addr[13] ^ network_addr[14] ^
			network_addr[15] ^ network_addr[16];

		return x & (NAT25_HASH_SIZE - 1);
#endif
	} else {
		unsigned long x = 0;
		int i;

		for (i = 0; i < MAX_NETWORK_ADDR_LEN; i++)
			x ^= network_addr[i];

		return x & (NAT25_HASH_SIZE - 1);
	}
}

static inline void __network_hash_link(struct rwnx_vif *vif,
				       struct nat25_network_db_entry *ent,
				       int hash)
{
	/* Caller must _enter_critical_bh already! */
	/* _irqL irqL; */
	/* _enter_critical_bh(&priv->br_ext_lock, &irqL); */

	ent->next_hash = vif->nethash[hash];
	if (ent->next_hash)
		ent->next_hash->pprev_hash = &ent->next_hash;
	vif->nethash[hash] = ent;
	ent->pprev_hash = &vif->nethash[hash];

	/* _exit_critical_bh(&priv->br_ext_lock, &irqL); */
}

static inline void __network_hash_unlink(struct nat25_network_db_entry *ent)
{
	/* Caller must _enter_critical_bh already! */
	/* _irqL irqL; */
	/* _enter_critical_bh(&priv->br_ext_lock, &irqL); */

	*ent->pprev_hash = ent->next_hash;
	if (ent->next_hash)
		ent->next_hash->pprev_hash = ent->pprev_hash;
	ent->next_hash = NULL;
	ent->pprev_hash = NULL;

	/* _exit_critical_bh(&priv->br_ext_lock, &irqL); */
}

static int __nat25_db_network_lookup_and_replace(struct rwnx_vif *vif,
						 struct sk_buff *skb,
						 unsigned char *network_addr)
{
	struct nat25_network_db_entry *db;

	spin_lock_bh(&vif->br_ext_lock);

	db = vif->nethash[__nat25_network_hash(network_addr)];
	while (db) {
		if (!memcmp(db->network_addr, network_addr, MAX_NETWORK_ADDR_LEN)) {
			if (!__nat25_has_expired(vif, db)) {
				/* replace the destination mac address */
				memcpy(skb->data, db->mac_addr, ETH_ALEN);
				atomic_inc(&db->use_count);

#ifdef CL_IPV6_PASS
				pr_info("NAT25: Lookup M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
					db->mac_addr[0], db->mac_addr[1], db->mac_addr[2],
					db->mac_addr[3], db->mac_addr[4], db->mac_addr[5],
					db->network_addr[0], db->network_addr[1],
					db->network_addr[2], db->network_addr[3],
					db->network_addr[4], db->network_addr[5],
					db->network_addr[6], db->network_addr[7],
					db->network_addr[8], db->network_addr[9],
					db->network_addr[10], db->network_addr[11],
					db->network_addr[12], db->network_addr[13],
					db->network_addr[14], db->network_addr[15],
					db->network_addr[16]);
#else
				pr_info("NAT25: Lookup M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
					db->mac_addr[0], db->mac_addr[1], db->mac_addr[2],
					db->mac_addr[3], db->mac_addr[4], db->mac_addr[5],
					db->network_addr[0], db->network_addr[1],
					db->network_addr[2], db->network_addr[3],
					db->network_addr[4], db->network_addr[5],
					db->network_addr[6], db->network_addr[7],
					db->network_addr[8], db->network_addr[9],
					db->network_addr[10]);
#endif
			}
			spin_unlock_bh(&vif->br_ext_lock);
			return 1;
		}

		db = db->next_hash;
	}

	spin_unlock_bh(&vif->br_ext_lock);
	return 0;
}

static void __nat25_db_network_insert(struct rwnx_vif *vif,
				      unsigned char *mac_addr,
				      unsigned char *network_addr)
{
	struct nat25_network_db_entry *db;
	int hash;

	spin_lock_bh(&vif->br_ext_lock);

	hash = __nat25_network_hash(network_addr);
	db = vif->nethash[hash];

	while (db) {
		if (!memcmp(db->network_addr, network_addr, MAX_NETWORK_ADDR_LEN)) {
			memcpy(db->mac_addr, mac_addr, ETH_ALEN);
			db->ageing_timer = jiffies;
			spin_unlock_bh(&vif->br_ext_lock);
			return;
		}

		db = db->next_hash;
	}

	db = kmalloc(sizeof(*db), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!db) {
		spin_unlock_bh(&vif->br_ext_lock);
		return;
	}

	memcpy(db->network_addr, network_addr, MAX_NETWORK_ADDR_LEN);
	memcpy(db->mac_addr, mac_addr, ETH_ALEN);
	atomic_set(&db->use_count, 1);
	db->ageing_timer = jiffies;

	__network_hash_link(vif, db, hash);

	spin_unlock_bh(&vif->br_ext_lock);
}

static void __nat25_db_print(struct rwnx_vif *vif)
{
	spin_lock_bh(&vif->br_ext_lock);

#ifdef BR_SUPPORT_DEBUG
	static int counter;
	int i, j;
	struct nat25_network_db_entry *db;

	counter++;
	if ((counter % 16) != 0)
		return;

	for (i = 0, j = 0; i < NAT25_HASH_SIZE; i++) {
		db = vif->nethash[i];

		while (db) {
#ifdef CL_IPV6_PASS
			pr_info("NAT25: DB(%d) H(%02d) C(%d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				j, i, atomic_read(&db->use_count), db->mac_addr[0],
				db->mac_addr[1], db->mac_addr[2], db->mac_addr[3], db->mac_addr[4],
				db->mac_addr[5], db->network_addr[0], db->network_addr[1],
				db->network_addr[2], db->network_addr[3], db->network_addr[4],
				db->network_addr[5], db->network_addr[6], db->network_addr[7],
				db->network_addr[8], db->network_addr[9], db->network_addr[10],
				db->network_addr[11], db->network_addr[12], db->network_addr[13],
				db->network_addr[14], db->network_addr[15], db->network_addr[16]);
#else
			pr_info("NAT25: DB(%d) H(%02d) C(%d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				j, i, atomic_read(&db->use_count), db->mac_addr[0],
				db->mac_addr[1], db->mac_addr[2], db->mac_addr[3],
				db->mac_addr[4], db->mac_addr[5], db->network_addr[0],
				db->network_addr[1], db->network_addr[2], db->network_addr[3],
				db->network_addr[4], db->network_addr[5], db->network_addr[6],
				db->network_addr[7], db->network_addr[8], db->network_addr[9],
				db->network_addr[10]);
#endif
			j++;

			db = db->next_hash;
		}
	}
#endif

	spin_unlock_bh(&vif->br_ext_lock);
}

/*
 *	NAT2.5 interface
 */

void nat25_db_cleanup(struct rwnx_vif *vif)
{
	int i;

	spin_lock_bh(&vif->br_ext_lock);

	for (i = 0; i < NAT25_HASH_SIZE; i++) {
		struct nat25_network_db_entry *f;

		f = vif->nethash[i];
		while (f) {
			struct nat25_network_db_entry *g;

			g = f->next_hash;
			if (vif->scdb_entry == f) {
				memset(vif->scdb_mac, 0, ETH_ALEN);
				memset(vif->scdb_ip, 0, 4);
				vif->scdb_entry = NULL;
			}
			__network_hash_unlink(f);
			kfree(f);

			f = g;
		}
	}

	spin_unlock_bh(&vif->br_ext_lock);
}

static inline void clear_scdb_entry(struct rwnx_vif *vif,
				    struct nat25_network_db_entry *f)
{
	if (vif->scdb_entry == f) {
		memset(vif->scdb_mac, 0, ETH_ALEN);
		memset(vif->scdb_ip, 0, 4);
		vif->scdb_entry = NULL;
	}
}

void nat25_db_expire(struct rwnx_vif *vif)
{
	int i;

	spin_lock_bh(&vif->br_ext_lock);

	/* if(!priv->eth_br_ext_info.nat25_disable) */
	{
		for (i = 0; i < NAT25_HASH_SIZE; i++) {
			struct nat25_network_db_entry *f;

			f = vif->nethash[i];

			while (f) {
				struct nat25_network_db_entry *g;

				g = f->next_hash;

				if (__nat25_has_expired(vif, f)) {
					if (atomic_dec_and_test(&f->use_count)) {
#ifdef BR_SUPPORT_DEBUG
#ifdef CL_IPV6_PASS
						panic_printk("NAT25 Expire H(%02d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
							     i, f->mac_addr[0], f->mac_addr[1],
							     f->mac_addr[2], f->mac_addr[3],
							     f->mac_addr[4], f->mac_addr[5],
							     f->network_addr[0], f->network_addr[1],
							     f->network_addr[2], f->network_addr[3],
							     f->network_addr[4], f->network_addr[5],
							     f->network_addr[6], f->network_addr[7],
							     f->network_addr[8], f->network_addr[9],
							     f->network_addr[10],
							     f->network_addr[11],
							     f->network_addr[12],
							     f->network_addr[13],
							     f->network_addr[14],
							     f->network_addr[15],
							     f->network_addr[16]);
#else

						panic_printk("NAT25 Expire H(%02d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
							     i, f->mac_addr[0], f->mac_addr[1],
							     f->mac_addr[2], f->mac_addr[3],
							     f->mac_addr[4], f->mac_addr[5],
							     f->network_addr[0], f->network_addr[1],
							     f->network_addr[2], f->network_addr[3],
							     f->network_addr[4], f->network_addr[5],
							     f->network_addr[6], f->network_addr[7],
							     f->network_addr[8], f->network_addr[9],
							     f->network_addr[10]);
#endif
#endif
						clear_scdb_entry(vif, f);
						__network_hash_unlink(f);
						kfree(f);
					}
				}

				f = g;
			}
		}
	}

	spin_unlock_bh(&vif->br_ext_lock);
}

#ifdef SUPPORT_TX_MCAST2UNI
static int check_ip_mc_and_replace(struct rwnx_vif *vif, struct sk_buff *skb,
				   unsigned int *dst_ip)
{
	struct stat_info *pstat;
	struct list_head *phead, *plist;
	int i;

	phead = &vif->asoc_list;
	plist = phead->next;

	while (plist != phead) {
		pstat = list_entry(plist, struct stat_info, asoc_list);
		plist = plist->next;

		if (pstat->ipmc_num == 0)
			continue;

		for (i = 0; i < MAX_IP_MC_ENTRY; i++) {
			if (pstat->ipmc[i].used &&
			    !memcmp(&pstat->ipmc[i].mcmac[3], ((unsigned char *)dst_ip) + 1,
						3)) {
				memcpy(skb->data, pstat->ipmc[i].mcmac, ETH_ALEN);
				return 1;
			}
		}
	}
	return 0;
}
#endif

static inline void check_and_reinit_br_mac(struct rwnx_vif *vif)
{
	if ((*(u32 *)vif->br_mac) == 0 &&
	    (*(u16 *)(vif->br_mac + 4)) == 0) {
		pr_info("Re-init netdev_br_init() due to br_mac==0!\n");
		netdev_br_init(vif->ndev);
	}
}

static int nat25_handle_pppoe_packet(struct rwnx_vif *vif,
				     struct pppoe_hdr *ph,
				     struct sk_buff *skb,
				     unsigned char network_addr[MAX_NETWORK_ADDR_LEN])
{
	struct pppoe_tag *tag, *old_tag;
	unsigned char tag_buf[40];
	int old_tag_len = 0;
	unsigned short *p_magic;

	if (ph->sid == 0) {
		/* Discovery phase */
		if (ph->code != PADI_CODE && ph->code != PADR_CODE)
			return -EINVAL;

		if (vif->eth_br_ext_info.add_pppoe_tag) {
			tag = (struct pppoe_tag *)tag_buf;
			old_tag = __nat25_find_pppoe_tag(ph,
							 ntohs(PTT_RELAY_SID));

			if (old_tag) {
				/* Copy old value and delete it */
				old_tag_len = ntohs(old_tag->tag_len);

				if (old_tag_len + TAG_HDR_LEN +
				    MAGIC_CODE_LEN + RTL_RELAY_TAG_LEN >
				    sizeof(tag_buf)) {
					pr_err("SID tag length too long!\n");
					return -EINVAL;
				}

				memcpy(tag->tag_data + MAGIC_CODE_LEN +
				       RTL_RELAY_TAG_LEN,
				       old_tag->tag_data, old_tag_len);

				if (skb_pull_and_merge(skb,
						       (unsigned char *)old_tag,
						       TAG_HDR_LEN + old_tag_len)
						       < 0) {
					pr_err("skb_pull_and_merge() failed in PADI/R packet!\n");
					return -EINVAL;
				}

				ph->length = htons(ntohs(ph->length) -
						   TAG_HDR_LEN - old_tag_len);
			}

			tag->tag_type = PTT_RELAY_SID;
			tag->tag_len = htons(MAGIC_CODE_LEN +
					     RTL_RELAY_TAG_LEN + old_tag_len);

			/* Insert magic_code + client MAC */
			p_magic = (unsigned short *)tag->tag_data;
			*p_magic = htons(MAGIC_CODE);
			memcpy(tag->tag_data + MAGIC_CODE_LEN,
			       skb->data + ETH_ALEN, ETH_ALEN);

			if (__nat25_add_pppoe_tag(skb, tag) < 0)
				return -EINVAL;

			pr_info("NAT25: Insert PPPoE, forward %s packet\n",
				(ph->code == PADI_CODE) ? "PADI" : "PADR");

		} else {
			/* Not adding relay tag */
			if (vif->pppoe_connection_in_progress &&
			    memcmp(skb->data + ETH_ALEN,
				   vif->pppoe_addr, ETH_ALEN)) {
				pr_info("Discard PPPoE packet: another connection in progress!\n");
				return -EBUSY;
			}

			if (vif->pppoe_connection_in_progress == 0)
				memcpy(vif->pppoe_addr, skb->data + ETH_ALEN,
				       ETH_ALEN);

			vif->pppoe_connection_in_progress = WAIT_TIME_PPPOE;
		}

	} else {
		/* Session phase */
		pr_info("NAT25: Insert PPPoE session packet to %s\n",
			skb->dev->name);

		__nat25_generate_pppoe_network_addr(network_addr,
						    skb->data, &ph->sid);

		__nat25_db_network_insert(vif, skb->data + ETH_ALEN,
					  network_addr);

		__nat25_db_print(vif);

		if (!vif->eth_br_ext_info.add_pppoe_tag &&
		    vif->pppoe_connection_in_progress &&
		    !memcmp(skb->data + ETH_ALEN,
			    vif->pppoe_addr, ETH_ALEN))
			vif->pppoe_connection_in_progress = 0;
	}

	return 0;
}

int nat25_db_handle(struct rwnx_vif *vif, struct sk_buff *skb, int method)
{
	unsigned short protocol;
	unsigned char network_addr[MAX_NETWORK_ADDR_LEN];

	if (!skb)
		return -1;

	if (method <= NAT25_MIN || method >= NAT25_MAX)
		return -1;

	protocol = *((unsigned short *)(skb->data + 2 * ETH_ALEN));

	/*---------------------------------------------------*/
	/*                 Handle IP frame                  */
	/*---------------------------------------------------*/
	if (protocol == htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);

		if (((unsigned char *)(iph) + (iph->ihl << 2)) >=
			(skb->data + ETH_HLEN + skb->len)) {
			pr_err("NAT25: malformed IP packet !\n");
			return -1;
		}

		switch (method) {
		case NAT25_CHECK:
			return -1;

		case NAT25_INSERT: {
			/* some multicast with source IP is all zero, maybe other case is
			 * illegal
			 */
			/* in class A, B, C, host address is all zero or all one is illegal
			 */
			if (iph->saddr == 0)
				return 0;
			pr_info("NAT25: Insert IP, SA=%08x, DA=%08x\n", iph->saddr,
				iph->daddr);
			__nat25_generate_ipv4_network_addr(network_addr, &iph->saddr);
			/* record source IP address and , source mac address into db */
			__nat25_db_network_insert(vif, skb->data + ETH_ALEN, network_addr);

			__nat25_db_print(vif);
		}
			return 0;

		case NAT25_LOOKUP: {
			pr_info("NAT25: Lookup IP, SA=%08x, DA=%08x\n", iph->saddr,
				iph->daddr);
#ifdef SUPPORT_TX_MCAST2UNI
			if (vif->pshare->rf_ft_var.mc2u_disable ||
			    ((((OPMODE & (WIFI_STATION_STATE | WIFI_ASOC_STATE)) ==
				   (WIFI_STATION_STATE | WIFI_ASOC_STATE)) &&
				  !check_ip_mc_and_replace(vif, skb, &iph->daddr)) ||
				 (OPMODE & WIFI_ADHOC_STATE)))
#endif
			{
				__nat25_generate_ipv4_network_addr(network_addr, &iph->daddr);

				if (!__nat25_db_network_lookup_and_replace(vif, skb,
									   network_addr)) {
					if (*((unsigned char *)&iph->daddr + 3) == 0xff) {
						/* L2 is unicast but L3 is broadcast, make L2 bacome
						 * broadcast
						 */
						pr_info("NAT25: Set DA as broadcast\n");
						memset(skb->data, 0xff, ETH_ALEN);
					} else {
						/* forward unknown IP packet to upper TCP/IP */
						pr_info("NAT25: Replace DA with BR's MAC\n");
						check_and_reinit_br_mac(vif);
						memcpy(skb->data, vif->br_mac, ETH_ALEN);
					}
				}
			}
		}
			return 0;

		default:
			return -1;
		}
	}

	/*---------------------------------------------------*/
	/*                 Handle ARP frame                 */
	/*---------------------------------------------------*/
	else if (protocol == htons(ETH_P_ARP)) {
		struct arphdr *arp = (struct arphdr *)(skb->data + ETH_HLEN);
		unsigned char *arp_ptr = (unsigned char *)(arp + 1);
		unsigned int *sender, *target;

		if (arp->ar_pro != htons(ETH_P_IP)) {
			pr_err("NAT25: arp protocol unknown (%4x)!\n", htons(arp->ar_pro));
			return -1;
		}

		switch (method) {
		case NAT25_CHECK:
			return 0; /* skb_copy for all ARP frame */

		case NAT25_INSERT: {
			pr_info("NAT25: Insert ARP, MAC=%02x%02x%02x%02x%02x%02x\n",
				arp_ptr[0], arp_ptr[1], arp_ptr[2], arp_ptr[3], arp_ptr[4],
				arp_ptr[5]);

			/* change to ARP sender mac address to wlan STA address */
			memcpy(arp_ptr, vif->ndev->dev_addr, ETH_ALEN);

			arp_ptr += arp->ar_hln;
			sender = (unsigned int *)arp_ptr;

			__nat25_generate_ipv4_network_addr(network_addr, sender);

			__nat25_db_network_insert(vif, skb->data + ETH_ALEN, network_addr);

			__nat25_db_print(vif);
		}
			return 0;

		case NAT25_LOOKUP: {
			pr_info("NAT25: Lookup ARP\n");

			arp_ptr += arp->ar_hln;
			sender = (unsigned int *)arp_ptr;
			arp_ptr += (arp->ar_hln + arp->ar_pln);
			target = (unsigned int *)arp_ptr;

			__nat25_generate_ipv4_network_addr(network_addr, target);

			__nat25_db_network_lookup_and_replace(vif, skb, network_addr);

			/* change to ARP target mac address to Lookup result */
			arp_ptr = (unsigned char *)(arp + 1);
			arp_ptr += (arp->ar_hln + arp->ar_pln);
			memcpy(arp_ptr, skb->data, ETH_ALEN);
		}
			return 0;

		default:
			return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle IPX and Apple Talk frame          */
	/*---------------------------------------------------*/
	else if ((protocol == htons(ETH_P_IPX)) ||
		 (protocol == htons(ETH_P_ATALK)) ||
			 (protocol == htons(ETH_P_AARP))) {
		unsigned char ipx_header[2] = {0xFF, 0xFF};
		struct ipxhdr *ipx = NULL;
		struct elapaarp *ea = NULL;
		struct ddpehdr *ddp = NULL;
		unsigned char *frame_ptr = skb->data + ETH_HLEN;

		if (protocol == htons(ETH_P_IPX)) {
			pr_info("NAT25: Protocol=IPX (Ethernet II)\n");
			ipx = (struct ipxhdr *)frame_ptr;
		} else { /* if(protocol <= __constant_htons(ETH_FRAME_LEN)) */
			if (!memcmp(ipx_header, frame_ptr, 2)) {
				pr_info("NAT25: Protocol=IPX (Ethernet 802.3)\n");
				ipx = (struct ipxhdr *)frame_ptr;
			} else {
				unsigned char ipx_8022_type = 0xE0;
				unsigned char snap_8022_type = 0xAA;

				if (*frame_ptr == snap_8022_type) {
					unsigned char ipx_snap_id[5] = {0x0, 0x0, 0x0, 0x81,
									0x37}; /* IPX SNAP ID */
					unsigned char aarp_snap_id[5] = {
						0x00, 0x00, 0x00, 0x80,
						0xF3}; /* Apple Talk AARP SNAP ID */
					unsigned char ddp_snap_id[5] = {
						0x08, 0x00, 0x07, 0x80,
						0x9B}; /* Apple Talk DDP SNAP ID */

					frame_ptr += 3; /* eliminate the 802.2 header */

					if (!memcmp(ipx_snap_id, frame_ptr, 5)) {
						frame_ptr += 5; /* eliminate the SNAP header */

						pr_info("NAT25: Protocol=IPX (Ethernet SNAP)\n");
						ipx = (struct ipxhdr *)frame_ptr;
					} else if (!memcmp(aarp_snap_id, frame_ptr, 5)) {
						frame_ptr += 5; /* eliminate the SNAP header */

						ea = (struct elapaarp *)frame_ptr;
					} else if (!memcmp(ddp_snap_id, frame_ptr, 5)) {
						frame_ptr += 5; /* eliminate the SNAP header */

						ddp = (struct ddpehdr *)frame_ptr;
					} else {
						pr_info("NAT25: Protocol=Ethernet SNAP %02x%02x%02x%02x%02x\n",
							frame_ptr[0], frame_ptr[1], frame_ptr[2],
							frame_ptr[3], frame_ptr[4]);
						return -1;
					}
				} else if (*frame_ptr == ipx_8022_type) {
					frame_ptr += 3; /* eliminate the 802.2 header */

					if (!memcmp(ipx_header, frame_ptr, 2)) {
						pr_info("NAT25: Protocol=IPX (Ethernet 802.2)\n");
						ipx = (struct ipxhdr *)frame_ptr;
					} else {
						return -1;
					}
				}
			}
		}

		/*   IPX  */
		if (ipx) {
			unsigned int *ipx_net = NULL;
			unsigned short *ipx_socket = NULL;

			switch (method) {
			case NAT25_CHECK:
				if (!memcmp(skb->data + ETH_ALEN, ipx->ipx_source.node,
					    ETH_ALEN)) {
					pr_info("NAT25: Check IPX skb_copy\n");
					return 0;
				}
				return -1;

			case NAT25_INSERT: {
				pr_info("NAT25: Insert IPX, Dest=%08x,%02x%02x%02x%02x%02x%02x,%04x Source=%08x,%02x%02x%02x%02x%02x%02x,%04x\n",
					ipx->ipx_dest.net, ipx->ipx_dest.node[0],
					ipx->ipx_dest.node[1], ipx->ipx_dest.node[2],
					ipx->ipx_dest.node[3], ipx->ipx_dest.node[4],
					ipx->ipx_dest.node[5], ipx->ipx_dest.sock,
					ipx->ipx_source.net, ipx->ipx_source.node[0],
					ipx->ipx_source.node[1], ipx->ipx_source.node[2],
					ipx->ipx_source.node[3], ipx->ipx_source.node[4],
					ipx->ipx_source.node[5], ipx->ipx_source.sock);

				if (!memcmp(skb->data + ETH_ALEN, ipx->ipx_source.node,
					    ETH_ALEN)) {
					unsigned int *ipx_net = NULL;
					unsigned short *ipx_socket = NULL;

					pr_info("NAT25: Use IPX Net, and Socket as network addr\n");

					ipx_net = &ipx->ipx_source.net;
					ipx_socket = &ipx->ipx_source.sock;
					__nat25_generate_ipx_network_addr_with_socket(network_addr,
										      ipx_net,
										      ipx_socket);

					/* change IPX source node addr to wlan STA address */
					memcpy(ipx->ipx_source.node, vif->ndev->dev_addr, ETH_ALEN);
				} else {
					unsigned int *ipx_net = NULL;
					unsigned char *ipx_node = NULL;

					ipx_net = &ipx->ipx_source.net;
					ipx_node = ipx->ipx_source.node;
					__nat25_generate_ipx_network_addr_with_node(network_addr,
										    ipx_net,
										    ipx_node);
				}

				__nat25_db_network_insert(vif, skb->data + ETH_ALEN,
							  network_addr);

				__nat25_db_print(vif);
			}
				return 0;

			case NAT25_LOOKUP: {
				if (!memcmp(vif->ndev->dev_addr, ipx->ipx_dest.node,
					    ETH_ALEN)) {
					unsigned int *ipx_net = &ipx->ipx_dest.net;
					unsigned short *ipx_socket = &ipx->ipx_dest.sock;

					pr_info("NAT25: Lookup IPX, Modify Destination IPX Node addr\n");

					__nat25_generate_ipx_network_addr_with_socket(network_addr,
										      ipx_net,
										      ipx_socket);

					__nat25_db_network_lookup_and_replace(vif, skb,
									      network_addr);

					/* replace IPX destination node addr with Lookup destination
					 * MAC addr
					 */
					memcpy(ipx->ipx_dest.node, skb->data, ETH_ALEN);
				} else {
					unsigned int *ipx_net = &ipx->ipx_dest.net;
					unsigned char *ipx_node = ipx->ipx_dest.node;

					__nat25_generate_ipx_network_addr_with_node(network_addr,
										    ipx_net,
										    ipx_node);

					__nat25_db_network_lookup_and_replace(vif, skb,
									      network_addr);
				}
			}
				return 0;

			default:
				return -1;
			}
		}

		/*   AARP  */
		else if (ea) {
			/* Sanity check fields. */
			if (ea->hw_len != ETH_ALEN || ea->pa_len != AARP_PA_ALEN) {
				pr_err("NAT25: Appletalk AARP Sanity check fail!\n");
				return -1;
			}

			switch (method) {
			case NAT25_CHECK:
				return 0;

			case NAT25_INSERT: {
				/* change to AARP source mac address to wlan STA address */
				memcpy(ea->hw_src, vif->ndev->dev_addr, ETH_ALEN);

				pr_info("NAT25: Insert AARP, Source=%d,%d Destination=%d,%d\n",
					ea->pa_src_net, ea->pa_src_node, ea->pa_dst_net,
					ea->pa_dst_node);

				__nat25_generate_apple_network_addr(network_addr,
								    &ea->pa_src_net,
								    &ea->pa_src_node);

				__nat25_db_network_insert(vif, skb->data + ETH_ALEN,
							  network_addr);

				__nat25_db_print(vif);
			}
				return 0;

			case NAT25_LOOKUP: {
				pr_info("NAT25: Lookup AARP, Source=%d,%d Destination=%d,%d\n",
					ea->pa_src_net, ea->pa_src_node, ea->pa_dst_net,
					ea->pa_dst_node);

				__nat25_generate_apple_network_addr(network_addr,
								    &ea->pa_dst_net,
								    &ea->pa_dst_node);

				__nat25_db_network_lookup_and_replace(vif, skb, network_addr);

				/* change to AARP destination mac address to Lookup result */
				memcpy(ea->hw_dst, skb->data, ETH_ALEN);
			}
				return 0;

			default:
				return -1;
			}
		}

		/*   DDP  */
		else if (ddp) {
			switch (method) {
			case NAT25_CHECK:
				return -1;

			case NAT25_INSERT: {
				pr_info("NAT25: Insert DDP, Source=%d,%d Destination=%d,%d\n",
					ddp->deh_snet, ddp->deh_snode, ddp->deh_dnet,
					ddp->deh_dnode);

				__nat25_generate_apple_network_addr(network_addr, &ddp->deh_snet,
								    &ddp->deh_snode);

				__nat25_db_network_insert(vif, skb->data + ETH_ALEN,
							  network_addr);

				__nat25_db_print(vif);
			}
				return 0;

			case NAT25_LOOKUP: {
				pr_info("NAT25: Lookup DDP, Source=%d,%d Destination=%d,%d\n",
					ddp->deh_snet, ddp->deh_snode, ddp->deh_dnet,
					ddp->deh_dnode);

				__nat25_generate_apple_network_addr(network_addr, &ddp->deh_dnet,
								    &ddp->deh_dnode);

				__nat25_db_network_lookup_and_replace(vif, skb, network_addr);
			}
				return 0;

			default:
				return -1;
			}
		}

		return -1;
	}

	/*---------------------------------------------------*/
	/*                Handle PPPoE frame                */
	/*---------------------------------------------------*/
	else if ((protocol == htons(ETH_P_PPP_DISC)) ||
		 (protocol == htons(ETH_P_PPP_SES))) {
		struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
		unsigned short *pmagic;

		switch (method) {
		case NAT25_CHECK:
			if (ph->sid == 0)
				return 0;
			return 1;

		case NAT25_INSERT:
			return nat25_handle_pppoe_packet(vif, ph, skb,
							 network_addr);

		case NAT25_LOOKUP:
			if (ph->code == PADO_CODE || ph->code == PADS_CODE) {
				if (vif->eth_br_ext_info.add_pppoe_tag) {
					struct pppoe_tag *tag;
					unsigned char *ptr;
					unsigned short tag_type, tag_len;
					int offset = 0;

					ptr = __nat25_find_pppoe_tag(ph, ntohs(PTT_RELAY_SID));
					if (ptr == 0) {
						pr_err("Fail to find PTT_RELAY_SID in FADO!\n");
						return -1;
					}

					tag = (struct pppoe_tag *)ptr;
					tag_type = (unsigned short)((ptr[0] << 8) + ptr[1]);
					tag_len = (unsigned short)((ptr[2] << 8) + ptr[3]);

					if (tag_type != ntohs(PTT_RELAY_SID) ||
					    (tag_len < (MAGIC_CODE_LEN + RTL_RELAY_TAG_LEN))) {
						pr_err("Invalid PTT_RELAY_SID tag length [%d]!\n",
						       tag_len);
						return -1;
					}

					pmagic = (unsigned short *)tag->tag_data;
					if (ntohs(*pmagic) != MAGIC_CODE) {
						pr_err("Can't find MAGIC_CODE in %s packet!\n",
						       (ph->code == PADO_CODE ? "PADO" : "PADS"));
						return -1;
					}

					memcpy(skb->data, tag->tag_data + MAGIC_CODE_LEN, ETH_ALEN);

					if (tag_len > MAGIC_CODE_LEN + RTL_RELAY_TAG_LEN)
						offset = TAG_HDR_LEN;

					if (skb_pull_and_merge(skb, ptr + offset,
							       TAG_HDR_LEN + MAGIC_CODE_LEN +
							       RTL_RELAY_TAG_LEN - offset) <
						0) {
						pr_err("call skb_pull_and_merge() failed in PADO packet!\n");
						return -1;
					}
					ph->length = htons(ntohs(ph->length) -
							   (TAG_HDR_LEN + MAGIC_CODE_LEN +
							    RTL_RELAY_TAG_LEN - offset));
					if (offset > 0)
						tag->tag_len =
							htons(tag_len - MAGIC_CODE_LEN -
							      RTL_RELAY_TAG_LEN);

					pr_info("NAT25: Lookup PPPoE, forward %s Packet from %s\n",
						(ph->code == PADO_CODE ? "PADO" : "PADS"),
						   skb->dev->name);
				} else { /* not add relay tag */
					if (!vif->pppoe_connection_in_progress) {
						pr_err("Discard PPPoE packet due to no connection in progress!\n");
						return -1;
					}
					memcpy(skb->data, vif->pppoe_addr, ETH_ALEN);
					vif->pppoe_connection_in_progress = WAIT_TIME_PPPOE;
				}
			} else {
				if (ph->sid != 0) {
					pr_info("NAT25: Lookup PPPoE, lookup session packet from %s\n",
						skb->dev->name);
					__nat25_generate_pppoe_network_addr(network_addr,
									    skb->data +
									    ETH_ALEN,
									    &ph->sid);

					__nat25_db_network_lookup_and_replace(vif, skb,
									      network_addr);

					__nat25_db_print(vif);
				} else {
					return -1;
				}
			}
			return 0;

		default:
			return -1;
		}
	}

	/*---------------------------------------------------*/
	/*                 Handle EAP frame                 */
	/*---------------------------------------------------*/
	else if (protocol == htons(0x888e)) {
		switch (method) {
		case NAT25_CHECK:
			return -1;

		case NAT25_INSERT:
			return 0;

		case NAT25_LOOKUP:
			return 0;

		default:
			return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle C-Media proprietary frame         */
	/*---------------------------------------------------*/
	else if ((protocol == htons(0xe2ae)) ||
		 (protocol == htons(0xe2af))) {
		switch (method) {
		case NAT25_CHECK:
			return -1;

		case NAT25_INSERT:
			return 0;

		case NAT25_LOOKUP:
			return 0;

		default:
			return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle IPV6 frame                                */
	/*---------------------------------------------------*/
#ifdef CL_IPV6_PASS
	else if (protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + ETH_HLEN);

		if (sizeof(*iph) >= (skb->len - ETH_HLEN)) {
			pr_err("NAT25: malformed IPv6 packet !\n");
			return -1;
		}

		switch (method) {
		case NAT25_CHECK:
			if (skb->data[0] & 1)
				return 0;
			return -1;

		case NAT25_INSERT: {
			pr_info("NAT25: Insert IP, SA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x, DA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x\n",
				iph->saddr.s6_addr16[0], iph->saddr.s6_addr16[1],
				iph->saddr.s6_addr16[2], iph->saddr.s6_addr16[3],
				iph->saddr.s6_addr16[4], iph->saddr.s6_addr16[5],
				iph->saddr.s6_addr16[6], iph->saddr.s6_addr16[7],
				iph->daddr.s6_addr16[0], iph->daddr.s6_addr16[1],
				iph->daddr.s6_addr16[2], iph->daddr.s6_addr16[3],
				iph->daddr.s6_addr16[4], iph->daddr.s6_addr16[5],
				iph->daddr.s6_addr16[6], iph->daddr.s6_addr16[7]);

			if (memcmp(&iph->saddr,
				   "\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0",
					   16)) {
				__nat25_generate_ipv6_network_addr(network_addr,
								   (unsigned int *)&iph->saddr);
				__nat25_db_network_insert(vif, skb->data + ETH_ALEN,
							  network_addr);
				__nat25_db_print(vif);

				if (iph->nexthdr == IPPROTO_ICMPV6 &&
				    skb->len > (ETH_HLEN + sizeof(*iph) + 4)) {
					if (update_nd_link_layer_addr(skb->data +
								      ETH_HLEN +
								      sizeof(*iph),
								      skb->len -
								      ETH_HLEN -
								      sizeof(*iph),
								      vif->ndev->dev_addr)) {
						struct icmp6hdr *hdr =
							(struct icmp6hdr *)(skb->data + ETH_HLEN +
									    sizeof(*iph));
						hdr->icmp6_cksum = 0;
						hdr->icmp6_cksum =
						csum_ipv6_magic(&iph->saddr, &iph->daddr,
								iph->payload_len, IPPROTO_ICMPV6,
								csum_partial((__u8 *)hdr,
									     iph->payload_len,
									     0));
					}
				}
			}
		}
			return 0;

		case NAT25_LOOKUP:
			pr_info("NAT25: Lookup IP, SA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x, DA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x\n",
				iph->saddr.s6_addr16[0], iph->saddr.s6_addr16[1],
				iph->saddr.s6_addr16[2], iph->saddr.s6_addr16[3],
				iph->saddr.s6_addr16[4], iph->saddr.s6_addr16[5],
				iph->saddr.s6_addr16[6], iph->saddr.s6_addr16[7],
				iph->daddr.s6_addr16[0], iph->daddr.s6_addr16[1],
				iph->daddr.s6_addr16[2], iph->daddr.s6_addr16[3],
				iph->daddr.s6_addr16[4], iph->daddr.s6_addr16[5],
				iph->daddr.s6_addr16[6], iph->daddr.s6_addr16[7]);

			__nat25_generate_ipv6_network_addr(network_addr,
							   (unsigned int *)&iph->daddr);
			if (!__nat25_db_network_lookup_and_replace(vif, skb, network_addr)) {
#ifdef SUPPORT_RX_UNI2MCAST
				if (iph->daddr.s6_addr[0] == 0xff)
					convert_ipv6_mac_to_mc(skb);
#endif
			}
			return 0;

		default:
			return -1;
		}
	}
#endif /* CL_IPV6_PASS */

	return -1;
}

int nat25_handle_frame(struct rwnx_vif *vif, struct sk_buff *skb)
{
	// printk("%s : vif_type=%d\n",__func__,RWNX_VIF_TYPE(vif));
#ifdef BR_SUPPORT_DEBUG
	if (!vif->eth_br_ext_info.nat25_disable && (!(skb->data[0] & 1))) {
		pr_info("NAT25: Input Frame: DA=%02x%02x%02x%02x%02x%02x SA=%02x%02x%02x%02x%02x%02x\n",
			skb->data[0], skb->data[1], skb->data[2], skb->data[3],
			skb->data[4], skb->data[5], skb->data[6], skb->data[7],
			skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
	}
#endif

	if (!(skb->data[0] & 1)) {
		int is_vlan_tag = 0, i, retval = 0;
		unsigned short vlan_hdr = 0;

		if (*((unsigned short *)(skb->data + ETH_ALEN * 2)) ==
			htons(ETH_P_8021Q)) {
			is_vlan_tag = 1;
			vlan_hdr = *((unsigned short *)(skb->data + ETH_ALEN * 2 + 2));
			for (i = 0; i < 6; i++)
				*((unsigned short *)(skb->data + ETH_ALEN * 2 + 2 - i * 2)) =
					*((unsigned short *)(skb->data + ETH_ALEN * 2 - 2 - i * 2));
			skb_pull(skb, 4);
		}

		if (!vif->eth_br_ext_info.nat25_disable) {
			unsigned long irqL;

			spin_lock_bh(&vif->br_ext_lock);
			/*
			 *  This function look up the destination network address from
			 *  the NAT2.5 database. Return value = -1 means that the
			 *  corresponding network protocol is NOT support.
			 */
			if (!vif->eth_br_ext_info.nat25sc_disable &&
			    (*((unsigned short *)(skb->data + ETH_ALEN * 2)) ==
				 htons(ETH_P_IP)) &&
				!memcmp(vif->scdb_ip, skb->data + ETH_HLEN + 16, 4)) {
				memcpy(skb->data, vif->scdb_mac, ETH_ALEN);

				spin_unlock_bh(&vif->br_ext_lock);
			} else {
				spin_unlock_bh(&vif->br_ext_lock);

				retval = nat25_db_handle(vif, skb, NAT25_LOOKUP);
			}
		} else {
			if (((*((unsigned short *)(skb->data + ETH_ALEN * 2)) ==
				  htons(ETH_P_IP)) &&
				 !memcmp(vif->br_ip, skb->data + ETH_HLEN + 16, 4)) ||
				((*((unsigned short *)(skb->data + ETH_ALEN * 2)) ==
				  htons(ETH_P_ARP)) &&
				 !memcmp(vif->br_ip, skb->data + ETH_HLEN + 24, 4))) {
				/* for traffic to upper TCP/IP */
				retval = nat25_db_handle(vif, skb, NAT25_LOOKUP);
			}
		}

		if (is_vlan_tag) {
			skb_push(skb, 4);
			for (i = 0; i < 6; i++)
				*((unsigned short *)(skb->data + i * 2)) =
					*((unsigned short *)(skb->data + 4 + i * 2));
			*((unsigned short *)(skb->data + ETH_ALEN * 2)) =
				htons(ETH_P_8021Q);
			*((unsigned short *)(skb->data + ETH_ALEN * 2 + 2)) = vlan_hdr;
		}

		if (retval == -1) {
			/* DEBUG_ERR("NAT25: Lookup fail!\n"); */
			return -1;
		}
	}

	return 0;
}

#define SERVER_PORT    67
#define CLIENT_PORT    68
#define DHCP_MAGIC     0x63825363
#define BROADCAST_FLAG 0x8000

struct dhcp_message {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u32 cookie;
	u8 options[308]; /* 312 - cookie */
};

static inline void dhcp_fix_broadcast_flag(struct dhcp_message *dhcph,
					   struct udphdr *udph)
{
	int sum;

	if (dhcph->cookie == __constant_htonl(DHCP_MAGIC)) {
		/* match magic word */
		if (!(dhcph->flags & htons(BROADCAST_FLAG))) {
			/* if not broadcast */
			pr_info("DHCP: change flag of DHCP request to broadcast.\n");

			/* OR BROADCAST flag */
			dhcph->flags |= htons(BROADCAST_FLAG);

			/* recalculate checksum */
			sum = ~(udph->check) & 0xffff;
			sum += dhcph->flags;
			while (sum >> 16)
				sum = (sum & 0xffff) + (sum >> 16);
			udph->check = ~sum;
		}
	}
}

void dhcp_flag_bcast(struct rwnx_vif *vif, struct sk_buff *skb)
{
	if (!skb)
		return;
	// debug trace
	// print_hex_dump(KERN_ERR, "SKB DUMP: SKB->DATA== ", DUMP_PREFIX_NONE, 32,
	// 1, skb->data, 64,false);
	if (!vif->eth_br_ext_info.dhcp_bcst_disable) {
		unsigned short protocol =
			*((unsigned short *)(skb->data + 2 * ETH_ALEN));
		pr_info("%s  protocol: %04x\n", __func__, protocol);

		if (protocol == htons(ETH_P_IP)) { /* IP */
			struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);

			if (iph->protocol == IPPROTO_UDP) { /* UDP */
				struct udphdr *udph =
					(struct udphdr *)((u8 *)iph + (iph->ihl << 2));

				if (udph->source == htons(CLIENT_PORT) &&
				    udph->dest ==
					 htons(SERVER_PORT)) { /* DHCP request */
					struct dhcp_message *dhcph =
						(struct dhcp_message *)((u8 *)udph +
									sizeof(struct udphdr));
					dhcp_fix_broadcast_flag(dhcph, udph);
				}
			}
		}
	}
}

void *scdb_find_entry(struct rwnx_vif *vif, unsigned char *mac_addr,
		      unsigned char *ip_addr)
{
	unsigned char network_addr[MAX_NETWORK_ADDR_LEN];
	struct nat25_network_db_entry *db;
	int hash;

	__nat25_generate_ipv4_network_addr(network_addr, (unsigned int *)ip_addr);
	hash = __nat25_network_hash(network_addr);
	db = vif->nethash[hash];
	while (db) {
		if (!memcmp(db->network_addr, network_addr, MAX_NETWORK_ADDR_LEN))
			return (void *)db;

		db = db->next_hash;
	}

	return NULL;
}

#endif /* CONFIG_AIC8800_BR_SUPPORT */
