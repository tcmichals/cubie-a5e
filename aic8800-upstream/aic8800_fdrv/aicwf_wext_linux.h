/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_WEXT_LINUX_H_
#define _AICWF_WEXT_LINUX_H_

struct scanu_result_wext {
	struct list_head scanu_re_list;
	struct cfg80211_bss *bss;
	struct scanu_result_ind *ind;
	u32_l *payload;
};

void aicwf_set_wireless_ext(struct net_device *ndev, struct rwnx_hw *rwnx_hw);
void aicwf_scan_complete_event(struct net_device *dev);

#endif /* _AICWF_WEXT_LINUX_H_ */
