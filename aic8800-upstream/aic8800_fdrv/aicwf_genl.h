/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AICWF_GENL_H__
#define __AICWF_GENL_H__

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <net/genetlink.h>

#include "rwnx_mod_params.h"
#include "rwnx_msg_tx.h"
#include "rwnx_platform.h"
#include "aicwf_compat_8800d80.h"

#define GENL_FAMILY_NAME ("AICWF_NL")

#define AICWF_PKT_FILTER_MAX         20
#define AICWF_PKT_FILTER_PATTERN_MAX 100

enum aicwf_nl_commands {
	WL_NL_CMD_UNSPEC,
	WL_NL_CMD_MSG,
	WL_NL_CMD_GET_INFO,
	WL_NL_CMD_MAX,
};

enum aicwf_nl_attrs {
	WL_NL_ATTR_UNSPEC,
	WL_NL_ATTR_IFINDEX,
	WL_NL_ATTR_AP2CP,
	WL_NL_ATTR_CP2AP,
	WL_NL_ATTR_MAX,
};

enum { LOG_EXCESSIVE, LOG_DUMP, LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_ERROR };

extern int aic_debug_lvl;
extern struct dbg_rftest_cmd_cfm cfm;

#define genl_debug(level, fmt, args...)                                        \
	do {                                                                       \
		if ((level) >= aic_debug_lvl) {                                          \
			pr_debug("AICGENL: " fmt, ##args);                                   \
		}                                                                      \
	} while (0)

struct pkt_filter {
	u16_l id;
	u16_l offset;
	u16_l length;
	u16_l enable;
	u8_l mask[AICWF_PKT_FILTER_PATTERN_MAX];
	u8_l pattern[AICWF_PKT_FILTER_PATTERN_MAX];
	u16_l total_len;
};

struct pkt_filter_list {
	u16_l num;
	struct pkt_filter filter[AICWF_PKT_FILTER_MAX];
};

typedef int (*func_cmd)(struct rwnx_hw *, char *[], int, char *,
						unsigned long *);
struct cmd_table_ops {
	const char *name;
	func_cmd func;
};

int cmd_pktfilter_set(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		      char *r_buf, unsigned long *r_len);
int cmd_pktfilter_del(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		      char *r_buf, unsigned long *r_len);
int cmd_pktfilter_delall(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			 char *r_buf, unsigned long *r_len);
int cmd_pktfilter_list(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		       char *r_buf, unsigned long *r_len);
int cmd_pktfilter_enable(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			 char *r_buf, unsigned long *r_len);
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
int cmd_country_set(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		    char *r_buf, unsigned long *r_len);
int cmd_country_get(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		    char *r_buf, unsigned long *r_len);
#endif
int cmd_temp_get(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		 char *r_buf, unsigned long *r_len);
int cmd_get_fw_version(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		       char *r_buf, unsigned long *r_len);
int cmd_get_link_status(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len);
int cmd_get_auth_type(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		      char *r_buf, unsigned long *r_len);
int cmd_set_suspend_mode(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			 char *r_buf, unsigned long *r_len);
int cmd_set_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		     char *r_buf, unsigned long *r_len);
int cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		     char *r_buf, unsigned long *r_len);
int cmd_set_bt_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len);
int cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len);

int match_cmd_table(struct rwnx_hw *rwnx_hw, char *s_buf, char *r_buf,
		    unsigned long *r_len);
int aicwf_init_genl(struct rwnx_hw *rwnx_hw);
void aicwf_deinit_genl(struct rwnx_hw *rwnx_hw);
#endif
