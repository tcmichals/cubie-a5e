/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AIC_PRIV_CMD_H_
#define _AIC_PRIV_CMD_H_

#include "rwnx_defs.h"

enum {
	SET_TX,
	SET_TXSTOP,
	SET_TXTONE,
	SET_RX,
	GET_RX_RESULT,
	SET_RXSTOP,
	SET_RX_METER,
	SET_POWER,
	SET_XTAL_CAP,
	SET_XTAL_CAP_FINE,
	GET_EFUSE_BLOCK,
	SET_FREQ_CAL,
	SET_FREQ_CAL_FINE,
	GET_FREQ_CAL,
	SET_MAC_ADDR,
	GET_MAC_ADDR,
	SET_BT_MAC_ADDR,
	GET_BT_MAC_ADDR,
	SET_VENDOR_INFO,
	GET_VENDOR_INFO,
	RDWR_PWRMM,
	RDWR_PWRIDX,
	RDWR_PWRLVL = RDWR_PWRIDX,
	RDWR_PWROFST,
	RDWR_DRVIBIT,
	RDWR_EFUSE_PWROFST,
	RDWR_EFUSE_DRVIBIT,
	SET_PAPR,
	SET_CAL_XTAL,
	GET_CAL_XTAL_RES,
	SET_COB_CAL,
	GET_COB_CAL_RES,
	RDWR_EFUSE_USRDATA,
	SET_NOTCH,
	RDWR_PWROFSTFINE,
	RDWR_EFUSE_PWROFSTFINE,
	RDWR_EFUSE_SDIOCFG,
	RDWR_EFUSE_USBVIDPID,
	SET_SRRC,
	SET_FSS,
	RDWR_EFUSE_HE_OFF,
	SET_USB_OFF,
	SET_PLL_TEST,
	SET_ANT_MODE,
	GET_NOISE,
	RDWR_BT_EFUSE_PWROFST,
	EXEC_FLASH_OPER,
	RDWR_PWRADD2X,
	RDWR_EFUSE_PWRADD2X,

};

struct cmd_rf_settx {
	u8_l chan;
	u8_l bw;
	u8_l mode;
	u8_l rate;
	u16_l length;
	u16_l tx_intv_us;
	s8_l max_pwr;
};

struct cmd_rf_setfreq {
	u8_l val;
};

struct cmd_rf_rx {
	u8_l chan;
	u8_l bw;
};

struct cmd_rf_getefuse {
	u8_l block;
};

struct cmd_rf_setcobcal {
	u8_l dutid;
	u8_l chip_num;
	u8_l dis_xtal;
};

struct cob_result_ptr {
	u16_l dut_rcv_golden_num;
	u8_l golden_rcv_dut_num;
	s8_l rssi_static;
	s8_l snr_static;
	s8_l dut_rssi_static;
	u16_l reserved;
};

struct android_wifi_priv_cmd {
	char __user *buf;

	int used_len;

	int total_len;

};

#ifdef CONFIG_COMPAT
struct compat_android_wifi_priv_cmd {
	compat_caddr_t buf;

	int used_len;

	int total_len;

};

#endif /* CONFIG_COMPAT */

extern int reg_regdb_size;
extern u8_l vendor_extension_data[256];
extern int vendor_extension_len;

int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);
unsigned int command_strtoul(const char *cp, char **endp, unsigned int base);
int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len);

#endif /* _AIC_PRIV_CMD_H_ */
