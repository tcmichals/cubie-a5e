// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief private command definition
 *
 ******************************************************************************
 */

#include "aic_priv_cmd.h"
#include "aicwf_genl.h"
#include "aicwf_sdio.h"
#include "rwnx_debugfs.h"
#include "rwnx_defs.h"
#include "rwnx_main.h"
#include "rwnx_mod_params.h"
#include "rwnx_msg_tx.h"
#include "rwnx_platform.h"
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#ifdef CONFIG_AIC8800_POWER_LIMIT
#include "aicwf_compat_8800d80.h"
#endif

static void print_help(const char *cmd);
struct dbg_rftest_cmd_cfm cfm = {{
	0,
}};

struct cmd_ef_usrdata {
	u8_l func;
	u8_l cnt;
	u8_l reserved[2];
	u32_l usrdata[3];
};

#define CMD_MAXARGS 224

static int parse_line(char *line, char *argv[])
{
	int nargs = 0;

	while (nargs < CMD_MAXARGS) {
		/* skip any white space */
		while ((*line == ' ') || (*line == '\t'))
			++line;

		if (*line == '\0') { /* end of line, no more args    */
			argv[nargs] = NULL;
			return nargs;
		}

		/* Argument include space should be bracketed by quotation mark */
		if (*line == '\"') {
			/* Skip quotation mark */
			line++;

			/* Begin of argument string */
			argv[nargs++] = line;

			/* Until end of argument */
			while (*line && (*line != '\"'))
				++line;
		} else {
			argv[nargs++] = line; /* begin of argument string    */

			/* find end of string */
			while (*line && (*line != ' ') && (*line != '\t'))
				++line;
		}

		if (*line == '\0') { /* end of line, no more args    */
			argv[nargs] = NULL;
			return nargs;
		}

		*line++ = '\0'; /* terminate current arg     */
	}

	pr_info("** Too many args (max. %d) **\n", CMD_MAXARGS);

	return nargs;
}

unsigned int command_strtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned int result = 0, value, is_neg = 0;

	if (*cp == '0') {
		cp++;
		if ((*cp == 'x') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}
		if (!base)
			base = 8;
	}
	if (!base)
		base = 10;
	if (*cp == '-') {
		is_neg = 1;
		cp++;
	}
	while (isxdigit(*cp) &&
	       (value = isdigit(*cp) ? *cp - '0'
		: (islower(*cp) ? toupper(*cp) : *cp) - 'A' +
		10) < base) {
		result = result * base + value;
		cp++;
	}
	if (is_neg)
		result = (unsigned int)((int)result * (-1));

	if (endp)
		*endp = (char *)cp;
	return result;
}

/*
 * aic_priv_cmd handers.
 */
static int aic_priv_cmd_set_tx(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
			       char *command)
{
	struct cmd_rf_settx settx_param;
#ifdef CONFIG_AIC8800_POWER_LIMIT
	s8 max_pwr;
	struct txpwr_loss_conf txpwr_loss_tmp;
	struct txpwr_loss_conf *txpwr_loss;

	txpwr_loss = &txpwr_loss_tmp;
#endif

	if (argc < 6)
		return -EINVAL;

	settx_param.chan = command_strtoul(argv[1], NULL, 10);
	settx_param.bw = command_strtoul(argv[2], NULL, 10);
	settx_param.mode = command_strtoul(argv[3], NULL, 10);
	settx_param.rate = command_strtoul(argv[4], NULL, 10);
	settx_param.length = command_strtoul(argv[5], NULL, 10);
	if (argc > 6)
		settx_param.tx_intv_us = command_strtoul(argv[6], NULL, 10);
	else
		settx_param.tx_intv_us = 10000; // set default val 10ms
	settx_param.max_pwr = POWER_LEVEL_INVALID_VAL;
	AICWFDBG(LOGINFO, "txparam:%d,%d,%d,%d,%d,%d\n", settx_param.chan,
		 settx_param.bw, settx_param.mode, settx_param.rate,
			 settx_param.length, settx_param.tx_intv_us);

#ifdef CONFIG_AIC8800_POWER_LIMIT
	txpwr_loss = &txpwr_loss_tmp;
	get_userconfig_txpwr_loss(txpwr_loss);
	if (txpwr_loss->loss_enable_2g4 == 1)
		AICWFDBG(LOGINFO, "%s:loss_value_2g4: %d\r\n", __func__,
			 txpwr_loss->loss_value_2g4);
	if (txpwr_loss->loss_enable_5g == 1)
		AICWFDBG(LOGINFO, "%s:loss_value_5g: %d\r\n", __func__,
			 txpwr_loss->loss_value_5g);
	max_pwr = get_powerlimit_by_chnum(settx_param.chan, &enable);
	if (settx_param.chan >= 36) {
		if (txpwr_loss->loss_enable_5g == 1)
			max_pwr -= txpwr_loss->loss_value_5g;
	} else {
		if (txpwr_loss->loss_enable_2g4 == 1)
			max_pwr -= txpwr_loss->loss_value_2g4;
	}
	if (enable)
		settx_param.max_pwr = max_pwr;
	AICWFDBG(LOGINFO, "max_pwr:%d\n", settx_param.max_pwr);
#endif

	rwnx_send_rftest_req(rwnx_hw, SET_TX, sizeof(struct cmd_rf_settx),
			     (u8_l *)&settx_param, NULL);
	return 0;
}

static int aic_priv_cmd_set_txstop(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, SET_TXSTOP, 0, NULL, NULL);
	return 0;
}

static int aic_priv_cmd_set_rx(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
			       char *command)
{
	struct cmd_rf_rx setrx_param;

	if (argc < 3)
		return -EINVAL;
	setrx_param.chan = command_strtoul(argv[1], NULL, 10);
	setrx_param.bw = command_strtoul(argv[2], NULL, 10);
	rwnx_send_rftest_req(rwnx_hw, SET_RX, sizeof(struct cmd_rf_rx),
			     (u8_l *)&setrx_param, NULL);
	return 0;
}

static int aic_priv_cmd_get_rx_result(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, GET_RX_RESULT, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 8);
	return 8;
}

static int aic_priv_cmd_set_rxstop(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, SET_RXSTOP, 0, NULL, NULL);
	return 0;
}

static int aic_priv_cmd_set_tx_tone(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8_l func = 0;
	u8_l buf[2];
	s8_l freq_ = 0;

	AICWFDBG(LOGINFO, "%s argc:%d\n", argv[0], argc);
	if (argc == 2 || argc == 3) {
		AICWFDBG(LOGINFO, "argv 1:%s\n", argv[1]);
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
		if (argc == 3) {
			AICWFDBG(LOGINFO, "argv 2:%s\n", argv[2]);
			freq_ = (u8_l)command_strtoul(argv[2], NULL, 10);
		} else {
			freq_ = 0;
		};
		buf[0] = func;
		buf[1] = (u8_l)freq_;
		rwnx_send_rftest_req(rwnx_hw, SET_TXTONE, argc - 1, buf, NULL);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_set_rx_meter(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	s8_l freq = 0;

	freq = (int)command_strtoul(argv[1], NULL, 10);
	rwnx_send_rftest_req(rwnx_hw, SET_RX_METER, sizeof(freq), (u8_l *)&freq,
			     NULL);
	return 0;
}

static int aic_priv_cmd_set_set_power(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	return aic_chip_priv_cmd_set_power(rwnx_hw, argc, argv, command);
}

static int aic_priv_cmd_set_xtal_cap(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	u8_l xtal_cap;

	if (argc < 2)
		return -EINVAL;

	xtal_cap = command_strtoul(argv[1], NULL, 10);
	AICWFDBG(LOGINFO, "xtal_cap =%x\r\n", xtal_cap);
	rwnx_send_rftest_req(rwnx_hw, SET_XTAL_CAP, sizeof(xtal_cap),
			     (u8_l *)&xtal_cap, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_set_xtal_cap_fine(struct rwnx_hw *rwnx_hw, int argc,
					  char *argv[], char *command)
{
	u8_l xtal_cap_fine;

	if (argc < 2)
		return -EINVAL;

	xtal_cap_fine = command_strtoul(argv[1], NULL, 10);
	AICWFDBG(LOGINFO, "xtal_cap_fine =%x\r\n", xtal_cap_fine);
	rwnx_send_rftest_req(rwnx_hw, SET_XTAL_CAP_FINE, sizeof(xtal_cap_fine),
			     (u8_l *)&xtal_cap_fine, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_get_efuse_block(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	struct cmd_rf_getefuse getefuse_param;

	if (argc < 2)
		return -EINVAL;

	getefuse_param.block = command_strtoul(argv[1], NULL, 10);
	rwnx_send_rftest_req(rwnx_hw, GET_EFUSE_BLOCK, sizeof(struct cmd_rf_getefuse),
			     (u8_l *)&getefuse_param, &cfm);
	AICWFDBG(LOGINFO, "get val=%x\r\n", cfm.rftest_result[0]);
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_set_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	struct cmd_rf_setfreq cmd_setfreq;

	if (argc < 2)
		return -EINVAL;

	cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
	AICWFDBG(LOGINFO, "param:%x\r\n", cmd_setfreq.val);
	rwnx_send_rftest_req(rwnx_hw, SET_FREQ_CAL, sizeof(struct cmd_rf_setfreq),
			     (u8_l *)&cmd_setfreq, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_set_freq_cal_fine(struct rwnx_hw *rwnx_hw, int argc,
					  char *argv[], char *command)
{
	struct cmd_rf_setfreq cmd_setfreq;

	if (argc < 2)
		return -EINVAL;

	cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
	AICWFDBG(LOGINFO, "param:%x\r\n", cmd_setfreq.val);
	rwnx_send_rftest_req(rwnx_hw, SET_FREQ_CAL_FINE, sizeof(struct cmd_rf_setfreq),
			     (u8_l *)&cmd_setfreq, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	aic_chip_priv_cmd_get_freq_cal(rwnx_hw, argc, argv, command);
	return 4;
}

static int aic_priv_cmd_set_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	u8_l mac_addr[6];

	if (argc < 7)
		return -EINVAL;

	mac_addr[5] = command_strtoul(argv[1], NULL, 16);
	mac_addr[4] = command_strtoul(argv[2], NULL, 16);
	mac_addr[3] = command_strtoul(argv[3], NULL, 16);
	mac_addr[2] = command_strtoul(argv[4], NULL, 16);
	mac_addr[1] = command_strtoul(argv[5], NULL, 16);
	mac_addr[0] = command_strtoul(argv[6], NULL, 16);
	AICWFDBG(LOGINFO, "set macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		 mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	rwnx_send_rftest_req(rwnx_hw, SET_MAC_ADDR, sizeof(mac_addr),
			     (u8_l *)&mac_addr, NULL);
	return 0;
}

static int aic_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	aic_chip_priv_cmd_get_mac_addr(rwnx_hw, argc, argv, command);
	return 8;
}

static int aic_priv_cmd_set_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	u8_l mac_addr[6];

	if (argc < 7)
		return -EINVAL;

	mac_addr[5] = command_strtoul(argv[1], NULL, 16);
	mac_addr[4] = command_strtoul(argv[2], NULL, 16);
	mac_addr[3] = command_strtoul(argv[3], NULL, 16);
	mac_addr[2] = command_strtoul(argv[4], NULL, 16);
	mac_addr[1] = command_strtoul(argv[5], NULL, 16);
	mac_addr[0] = command_strtoul(argv[6], NULL, 16);
	AICWFDBG(LOGINFO, "set bt macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		 mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	rwnx_send_rftest_req(rwnx_hw, SET_BT_MAC_ADDR, sizeof(mac_addr),
			     (u8_l *)&mac_addr, NULL);
	return 0;
}

static int aic_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	aic_chip_priv_cmd_get_bt_mac_addr(rwnx_hw, argc, argv, command);
	return 8;
}

static int aic_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	return aic_chip_priv_cmd_set_vendor_info(rwnx_hw, argc, argv, command);
}

static int aic_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	return aic_chip_priv_cmd_get_vendor_info(rwnx_hw, argc, argv, command);
}

static int aic_priv_cmd_rdwr_pwrmm(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	if (argc <= 1) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRMM, 0, NULL, &cfm);
	} else { // write
		u8_l pwrmm = (u8_l)command_strtoul(argv[1], NULL, 16);

		pwrmm = (pwrmm) ? 1 : 0;
		AICWFDBG(LOGINFO, "set pwrmm = %x\r\n", pwrmm);
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRMM, sizeof(pwrmm), (u8_l *)&pwrmm,
				     &cfm);
	}
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_rdwr_pwridx(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8_l func = 0;

	if (!rwnx_hw->chip_ops->support_priv_cmd_rdwr_pwridx) {
		AICWFDBG(LOGERROR, "unsupported cmd\n");
		return -EINVAL;
	}
	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRIDX, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr idx
		if (argc > 3) {
			u8_l type = (u8_l)command_strtoul(argv[2], NULL, 16);
			u8_l pwridx = (u8_l)command_strtoul(argv[3], NULL, 10);
			u8_l buf[3] = {func, type, pwridx};

			AICWFDBG(LOGINFO, "set pwridx:[%x][%x]=%x\r\n", func, type, pwridx);
			rwnx_send_rftest_req(rwnx_hw, RDWR_PWRIDX, sizeof(buf), buf, &cfm);
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 9);
	return 9;
}

static int aic_priv_cmd_rdwr_pwrlvl(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8_l func = 0;

	if (!rwnx_hw->chip_ops->support_priv_cmd_rdwr_pwrlvl) {
		AICWFDBG(LOGERROR, "unsupported cmd\n");
		return -EINVAL;
	}
	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRLVL, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr lvl
		if (argc > 4) {
			u8_l grp = (u8_l)command_strtoul(argv[2], NULL, 16);
			u8_l idx, size;
			u8_l buf[14] = {
				func,
				grp,
			};
			if (argc > 12) { // set all grp
				AICWFDBG(LOGINFO,
					 "set pwrlvl %s:\n"
						 "  [%x] =",
						 (func == 1) ? "2.4g" : "5g", grp);
				if (grp == 1) { // TXPWR_LVL_GRP_11N_11AC
					size = 10;
				} else {
					size = 12;
				}
				for (idx = 0; idx < size; idx++) {
					s8_l pwrlvl =
						(s8_l)command_strtoul(argv[3 + idx], NULL, 10);
					buf[2 + idx] = (u8_l)pwrlvl;
					if (idx && !(idx & 0x3))
						AICWFDBG(LOGINFO, " ");
					AICWFDBG(LOGINFO, " %2d", pwrlvl);
				}
				AICWFDBG(LOGINFO, "\n");
				size += 2;
			} else { // set grp[idx]
				u8_l idx = (u8_l)command_strtoul(argv[3], NULL, 10);
				s8_l pwrlvl = (s8_l)command_strtoul(argv[4], NULL, 10);

				buf[2] = idx;
				buf[3] = (u8_l)pwrlvl;
				size = 4;
				AICWFDBG(LOGINFO,
					 "set pwrlvl %s:\n"
						 "  [%x][%d] = %d\n",
						 (func == 1) ? "2.4g" : "5g", grp, idx, pwrlvl);
			}
			rwnx_send_rftest_req(rwnx_hw, RDWR_PWRLVL, size, buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}

	memcpy(command, &cfm.rftest_result[0],  (rwnx_hw->chip_ops->pwrlvl_result_copy_len) * 12);
	return ((rwnx_hw->chip_ops->pwrlvl_result_copy_len) * 12);
}

static int aic_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	return aic_chip_priv_cmd_rdwr_pwrofst(rwnx_hw, argc, argv, command);
}

static int aic_priv_cmd_rdwr_pwrofstfine(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFSTFINE, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr ofst
		if (argc > 3) {
			u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
			s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
			u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};

			AICWFDBG(LOGINFO, "set pwrofstfine:[%x][%x]=%d\r\n", func, chgrp,
				 pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFSTFINE, sizeof(buf), buf,
					     &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 7);
	return 7;
}

static int aic_priv_cmd_rdwr_drvibit(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_DRVIBIT, 0, NULL, &cfm);
	} else if (func == 1) { // write 2.4g pa drv_ibit
		if (argc > 2) {
			u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
			u8_l buf[2] = {func, ibit};

			AICWFDBG(LOGINFO, "set drvibit:[%x]=%x\r\n", func, ibit);
			rwnx_send_rftest_req(rwnx_hw, RDWR_DRVIBIT, sizeof(buf), buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 16);
	return 16;
}

static int aic_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	return aic_chip_priv_cmd_rdwr_efuse_pwrofst(rwnx_hw, argc, argv, command);
}

static int aic_priv_cmd_rdwr_efuse_pwrofstfine(struct rwnx_hw *rwnx_hw,
					       int argc, char *argv[],
					       char *command)
{
	u8_l func = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFSTFINE, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr ofst
		if (argc > 3) {
			u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
			s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
			u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};

			AICWFDBG(LOGINFO, "set pwrofstfine:[%x][%x]=%d\r\n", func, chgrp,
				 pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFSTFINE, sizeof(buf),
					     buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 7);
	return 7;
}

static int aic_priv_cmd_rdwr_efuse_drvibit(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_DRVIBIT, 0, NULL, &cfm);
	} else if (func == 1) { // write 2.4g pa drv_ibit
		if (argc > 2) {
			u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
			u8_l buf[2] = {func, ibit};

			AICWFDBG(LOGINFO, "set efuse drvibit:[%x]=%x\r\n", func, ibit);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_DRVIBIT, sizeof(buf), buf,
					     &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_rdwr_efuse_usrdata(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	struct cmd_ef_usrdata cmd_ef_usrdata;

	if (argc <= 1) { // read all
		cmd_ef_usrdata.func = 0;
		cmd_ef_usrdata.cnt = 3;
	} else if (argc >= 2) { // read/write
		cmd_ef_usrdata.func = (u8_l)command_strtoul(argv[1], NULL, 10);
		cmd_ef_usrdata.cnt = (u8_l)command_strtoul(argv[2], NULL, 10);
		if (cmd_ef_usrdata.func == 1) {
			int idx;

			for (idx = 0; idx < cmd_ef_usrdata.cnt; idx++) {
				cmd_ef_usrdata.usrdata[idx] =
					(u32_l)command_strtoul(argv[3 + idx], NULL, 16);
			}
		}
	} else {
		AICWFDBG(LOGERROR, "wrong argc: %x\n", argc);
		return -EINVAL;
	}
	rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_USRDATA, sizeof(cmd_ef_usrdata),
			     (u8_l *)&cmd_ef_usrdata, &cfm);
	memcpy(command, &cfm.rftest_result[0], 12);
	return 12;
}

static int aic_priv_cmd_rdwr_efuse_sdiocfg(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_SDIOCFG, 0, NULL, &cfm);
	} else if (func == 1) { // write sdiocfg
		if (argc > 2) {
			u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
			u8_l buf[2] = {func, ibit};

			AICWFDBG(LOGINFO, "set efuse sdiocfg:[%x]=%x\r\n", func, ibit);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_SDIOCFG, sizeof(buf), buf,
					     &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_rdwr_efuse_usbvidpid(struct rwnx_hw *rwnx_hw, int argc,
					     char *argv[], char *command)
{
	u8_l func = 0;

	AICWFDBG(LOGINFO, "read/write usb vid/pid into efuse\n");
	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_USBVIDPID, 0, NULL, &cfm);
	} else if (func == 1) { // write USB vid+pid
		if (argc > 2) {
			u32_l usb_id = (u32_l)command_strtoul(argv[2], NULL, 16);
			u8_l buf[5] = {func, (u8_l)usb_id, (u8_l)(usb_id >> 8),
						   (u8_l)(usb_id >> 16), (u8_l)(usb_id >> 24)};
			AICWFDBG(LOGINFO, "set efuse usb vid/pid:[%x]=%x\r\n", func,
				 usb_id);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_USBVIDPID, sizeof(buf),
					     buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 4);
	return 4;
}

static int aic_priv_cmd_rdwr_efuse_he_off(struct rwnx_hw *rwnx_hw, int argc,
					  char *argv[], char *command)
{
	u8_l func = 0;

	func = command_strtoul(argv[1], NULL, 10);
	AICWFDBG(LOGINFO, "set he off: %d\n", func);
	if (func == 1 || func == 0) {
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_HE_OFF, sizeof(func),
				     (u8_l *)&func, &cfm);
		AICWFDBG(LOGINFO, "he_off cfm: %d\n", cfm.rftest_result[0]);
		memcpy(command, &cfm.rftest_result[0], 4);
		return 4;
	}
	return 0;
}

static int aic_priv_cmd_set_cal_xtal(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, SET_CAL_XTAL, 0, NULL, NULL);
	return 0;
}

static int aic_priv_cmd_get_cal_xtal_res(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, GET_CAL_XTAL_RES, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	AICWFDBG(LOGINFO, "cap=0x%x, cap_fine=0x%x\n",
		 cfm.rftest_result[0] & 0x0000ffff,
			 (cfm.rftest_result[0] >> 16) & 0x0000ffff);
	return 4;
}

static int aic_priv_cmd_set_cob_cal(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	struct cmd_rf_setcobcal setcob_cal;

	if (argc < 3)
		return -EINVAL;
	setcob_cal.dutid = command_strtoul(argv[1], NULL, 10);
	setcob_cal.chip_num = command_strtoul(argv[2], NULL, 10);
	setcob_cal.dis_xtal = command_strtoul(argv[3], NULL, 10);
	rwnx_send_rftest_req(rwnx_hw, SET_COB_CAL, sizeof(struct cmd_rf_setcobcal),
			     (u8_l *)&setcob_cal, NULL);
	return 0;
}

static int aic_priv_cmd_get_cob_cal_res(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	u8_l state;
	struct cob_result_ptr *cob_result_ptr;

	rwnx_send_rftest_req(rwnx_hw, GET_COB_CAL_RES, 0, NULL, &cfm);
	state = (cfm.rftest_result[0] >> 16) & 0x000000ff;
	if (!state) {
		AICWFDBG(LOGINFO, "cap= 0x%x, cap_fine= 0x%x, freq_ofst= %d Hz\n",
			 cfm.rftest_result[0] & 0x000000ff,
				 (cfm.rftest_result[0] >> 8) & 0x000000ff,
				 cfm.rftest_result[1]);
		cob_result_ptr = (struct cob_result_ptr *)&cfm.rftest_result[2];
		AICWFDBG(LOGINFO,
			 "golden_rcv_dut= %d , tx_rssi= %d dBm, snr = %d dB\ndut_rcv_godlden= %d , rx_rssi= %d dBm",
			 cob_result_ptr->golden_rcv_dut_num,
			 cob_result_ptr->rssi_static, cob_result_ptr->snr_static,
			 cob_result_ptr->dut_rcv_golden_num,
			 cob_result_ptr->dut_rssi_static);
		memcpy(command, &cfm.rftest_result, 16);
		return 16;
	}
	AICWFDBG(LOGERROR, "cob not idle\n");
	return -EINVAL;
}

static int aic_priv_cmd_do_cob_test(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8_l state;
	struct cmd_rf_setcobcal setcob_cal;
	struct cob_result_ptr *cob_result_ptr;

	setcob_cal.dutid = 1;
	setcob_cal.chip_num = 1;
	setcob_cal.dis_xtal = 0;
	if (argc > 1)
		setcob_cal.dis_xtal = command_strtoul(argv[1], NULL, 10);
	rwnx_send_rftest_req(rwnx_hw, SET_COB_CAL, sizeof(struct cmd_rf_setcobcal),
			     (u8_l *)&setcob_cal, NULL);
	msleep(2000);
	rwnx_send_rftest_req(rwnx_hw, GET_COB_CAL_RES, 0, NULL, &cfm);
	state = (cfm.rftest_result[0] >> 16) & 0x000000ff;
	if (!state) {
		AICWFDBG(LOGINFO, "cap= 0x%x, cap_fine= 0x%x, freq_ofst= %d Hz\n",
			 cfm.rftest_result[0] & 0x000000ff,
				 (cfm.rftest_result[0] >> 8) & 0x000000ff,
				 cfm.rftest_result[1]);
		cob_result_ptr = (struct cob_result_ptr *)&cfm.rftest_result[2];
		AICWFDBG(LOGINFO,
			 "golden_rcv_dut= %d , tx_rssi= %d dBm, snr = %d dB\ndut_rcv_godlden= %d , rx_rssi= %d dBm",
			 cob_result_ptr->golden_rcv_dut_num,
			 cob_result_ptr->rssi_static, cob_result_ptr->snr_static,
			 cob_result_ptr->dut_rcv_golden_num,
			 cob_result_ptr->dut_rssi_static);
		memcpy(command, &cfm.rftest_result, 16);
		return 16;
	}
	AICWFDBG(LOGERROR, "cob not idle\n");
	return -EINVAL;
}

static int aic_priv_cmd_set_papr(struct rwnx_hw *rwnx_hw, int argc,
				 char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1) {
		func = command_strtoul(argv[1], NULL, 10);
		AICWFDBG(LOGINFO, "papr %d\r\n", func);
		rwnx_send_rftest_req(rwnx_hw, SET_PAPR, sizeof(func), (u8_l *)&func,
				     NULL);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_set_notch(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1) {
		func = command_strtoul(argv[1], NULL, 10);
		AICWFDBG(LOGINFO, "notch %d\r\n", func);
		rwnx_send_rftest_req(rwnx_hw, SET_NOTCH, sizeof(func), (u8_l *)&func,
				     NULL);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_set_srrc(struct rwnx_hw *rwnx_hw, int argc,
				 char *argv[], char *command)
{
	u8_l func = 0;

	if (argc > 1) {
		func = command_strtoul(argv[1], NULL, 10);
		AICWFDBG(LOGINFO, "srrc %d\r\n", func);
		rwnx_send_rftest_req(rwnx_hw, SET_SRRC, sizeof(func), (u8_l *)&func,
				     NULL);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_set_fss(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
				char *command)
{
	u8_l func = 0;

	if (argc > 1) {
		func = command_strtoul(argv[1], NULL, 10);
		AICWFDBG(LOGINFO, "fss %d\r\n", func);
		rwnx_send_rftest_req(rwnx_hw, SET_FSS, sizeof(func), (u8_l *)&func,
				     NULL);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_set_usb_off(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	rwnx_send_rftest_req(rwnx_hw, SET_USB_OFF, 0, NULL, NULL);
	return 0;
}

static int aic_priv_cmd_set_pll_test(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	u8_l func = 0, tx_pwr = 0xc;
	s8_l freq = 0;

	if (argc > 1)
		func = command_strtoul(argv[1], NULL, 16);
	if (argc > 3) {
		freq = (s8_l)command_strtoul(argv[2], NULL, 10);
		tx_pwr = command_strtoul(argv[3], NULL, 16);
	}
	if (func <= 1) {
		u8_l buf[3] = {func, (u8_l)freq, tx_pwr};

		AICWFDBG(LOGINFO, "set pll_test %d: freq=%d, tx_pwr=0x%x\n", func, freq,
			 tx_pwr);
		rwnx_send_rftest_req(rwnx_hw, SET_PLL_TEST, sizeof(buf), buf, &cfm);
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_get_txpwr(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	s8_l power = 0;

	power = get_txpwr_max(power);
	memcpy(command, &power, 1);
	return 1;
}

static int aic_priv_cmd_set_txpwr_loss(struct rwnx_hw *rwnx_hw, int argc,
				       char *argv[], char *command)
{
	s8_l func;

	if (argc > 1) {
		func = (s8_l)command_strtoul(argv[1], NULL, 10);
		pr_info("set txpwr loss: %d\n", func);
		struct rwnx_hw *hw = rwnx_platform_get_hw(g_rwnx_plat);
		if (hw && hw->chip_ops && hw->chip_ops->support_priv_cmd_set_txpwr_loss) {
			set_txpwr_loss_ofst(func);
			rwnx_send_txpwr_lvl_v3_req(hw, 0);
		} else {
			AICWFDBG(LOGINFO, "error:don't support ,now only support D40 D80");
		}
	} else {
		pr_info("wrong args\n");
		return -EINVAL;
	}
	return 0;
}

static int aic_priv_cmd_rdwr_pwradd2x(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	u8_l func = 0;
	u8_l buf[2];
	s8 pwradd2x_in = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 10);

	if (func > 0 && argc > 2)
		pwradd2x_in = (int8_t)command_strtoul(argv[2], NULL, 10);

	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRADD2X, 0, NULL, &cfm);
	} else if (func == 1 || func == 2) { // write pwradd2x
		AICWFDBG(LOGINFO, "set pwradd2x_%s %d\r\n",
			 (func == 1) ? "2g4" : "5g", pwradd2x_in);
		if (pwradd2x_in < -15 || pwradd2x_in > 15) {
			AICWFDBG(LOGERROR,
				 "wrong params %d,  pwradd2x: -15 ~ 15\n",
				 pwradd2x_in);
			return -EINVAL;
		}

		buf[0] = func;
		buf[1] = (u8_l)pwradd2x_in;
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWRADD2X,
				     sizeof(buf), buf, &cfm);
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 2);
	return 2;
}

static int aic_priv_cmd_rdwr_efuse_pwradd2x(struct rwnx_hw *rwnx_hw, int argc,
					    char *argv[], char *command)
{
	u8_l func = 0;
	u8_l buf[2];
	s8 pwradd2x_in = 0;

	if (argc > 1)
		func = (u8_l)command_strtoul(argv[1], NULL, 10);

	if (func > 0 && argc > 2)
		pwradd2x_in = (int8_t)command_strtoul(argv[2], NULL, 10);

	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWRADD2X, 0, NULL, &cfm);
	} else if (func == 1 || func == 2) { // write pwradd2x
		AICWFDBG(LOGINFO, "set efuse pwradd2x_%s %d\r\n",
			 (func == 1) ? "2g4" : "5g", pwradd2x_in);
		if (pwradd2x_in < -15 || pwradd2x_in > 15) {
			AICWFDBG(LOGERROR,
				 "wrong params %d,  pwradd2x: -15 ~ 15\n",
				 pwradd2x_in);
			return -EINVAL;
		}

		buf[0] = func;
		buf[1] = (u8_l)pwradd2x_in;
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWRADD2X,
				     sizeof(buf), buf, &cfm);
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	memcpy(command, &cfm.rftest_result[0], 3);
	return 3;
}

#ifdef CONFIG_AIC8800_AUTO_CUSTREG
static int aic_priv_cmd_country_set(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	int ret = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	AICWFDBG(LOGINFO, "cmd country_set: %s\n", argv[1]);
	if (strncmp(argv[1], "AUTO", 4) == 0) {
		rwnx_hw->ccode.auto_set = true;
		rwnx_hw->ccode.ccode_set = false;
		rwnx_hw->ccode.ccode_cnt = 0;
		rwnx_hw->ccode.ccode_rssi = -100;
	} else if (strncmp(argv[1], "MANUAL", 6) == 0) {
		rwnx_hw->ccode.auto_set = false;
		rwnx_hw->ccode.ccode_set = true;
	} else {
		rwnx_hw->ccode.ccode_set = true;
		ret = rwnx_regulatory_set_wiphy_regd(rwnx_hw->wiphy,
						     get_regdomain_from_rwnx_db(rwnx_hw->wiphy,
										argv[1]));
		memcpy(rwnx_hw->country_abbr, argv[1], 2);
		rwnx_hw->ccode.ccode_cnt = 0;
#ifdef CONFIG_AIC8800_REGION_PW
		rwnx_send_txpwr_lvl_v3_req(rwnx_hw, get_ccode_region(rwnx_hw->country_abbr));
#endif
#ifdef CONFIG_AIC8800_POWER_LIMIT
		aic_chip_powerlimit_load(rwnx_hw);
		if (!rwnx_hw->testmode)
			rwnx_send_me_chan_config_req(rwnx_hw);
#endif
	}
	return ret;
}

static int aic_priv_cmd_country_get(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	const struct ieee80211_regdomain *regd;
	u8_l buf[3];
	int bytes_written = 0;

	rcu_read_lock();
	regd = rcu_dereference(rwnx_hw->wiphy->regd);
	if (!regd) {
		rcu_read_unlock();
		return -ENODATA;
	}
	buf[0] = regd->alpha2[0];
	buf[1] = regd->alpha2[1];
	rcu_read_unlock();
	buf[2] = rwnx_hw->ccode.auto_set;
	memcpy(command, &buf[0], 3);
	bytes_written = 3;
	AICWFDBG(LOGINFO, "cmd country_get: %c%c\n", command[0], command[1]);

	return bytes_written;
}
#endif

#ifdef CONFIG_AIC8800_TEMP_CONTROL
static int aic_priv_cmd_temp_ctrl_sw(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	if (command_strtoul(argv[1], NULL, 10) == 0) {
		AICWFDBG(LOGINFO, "tp to off\n");
		rwnx_hw->sdiodev->tp_ctrl.on_off = false;
		rwnx_hw->sdiodev->tp_ctrl.get_level = 0;
		spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
		rwnx_hw->sdiodev->tp_ctrl.tm_start = 0;
		if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer))
			//del_timer_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
			timer_delete_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
		spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
	} else if (command_strtoul(argv[1], NULL, 10) == 1) {
		AICWFDBG(LOGINFO, "tp to on\n");
		rwnx_hw->sdiodev->tp_ctrl.on_off = true;
		spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
		rwnx_hw->sdiodev->tp_ctrl.tm_start = 1;
		mod_timer(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer,
			  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
		spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
	} else {
		AICWFDBG(LOGINFO, "tp err param\n");
		return -1;
	}

	return 0;
}

static int aic_priv_cmd_temp_sget(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	u8_l func = 0;
	int bytes_written = 0;
	s8 tp_res[4];

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	func = (u8_l)command_strtoul(argv[1], NULL, 10);
	if (func == 0) {                            // get
		if (rwnx_hw->sdiodev->tp_ctrl.on_off) { // on
			tp_res[0] = 1;
			if (rwnx_hw->sdiodev->tp_ctrl.set_level == 0)
				tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.get_level;
			else
				tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.set_level;
			AICWFDBG(LOGINFO, "tp_get on-off: %d, ctrl-level: %d\n", tp_res[0],
				 tp_res[1]);
			memcpy(command, &tp_res[0], 2);
			bytes_written = 2;
		} else { // off
			tp_res[0] = 0;
			AICWFDBG(LOGINFO, "tp_get on-off: %d\n", tp_res[0]);
			memcpy(command, &tp_res[0], 1);
			bytes_written = 1;
		}
	} else if (func == 1) { // set
		if (!rwnx_hw->sdiodev->tp_ctrl.on_off) {
			AICWFDBG(LOGINFO, "tp_set sw is off, return\n");
			tp_res[0] = 0;
			memcpy(command, &tp_res[0], 1);
			bytes_written = 1;
		} else {
			if (argc < 3) {
				AICWFDBG(LOGINFO, "%s param err\n", __func__);
				return -1;
			}
			rwnx_hw->sdiodev->tp_ctrl.set_level =
				command_strtoul(argv[2], NULL, 10);
			if (rwnx_hw->sdiodev->tp_ctrl.set_level < 0 ||
			    rwnx_hw->sdiodev->tp_ctrl.set_level > 2) {
				AICWFDBG(LOGINFO, "set_level out of range\n");
				rwnx_hw->sdiodev->tp_ctrl.set_level = 0;
			}
			rwnx_hw->sdiodev->tp_ctrl.get_level = 0;
			tp_res[0] = 1;
			tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.set_level;
			AICWFDBG(LOGINFO, "tp_set ctrl-level: %d\n",
				 rwnx_hw->sdiodev->tp_ctrl.set_level);
			memcpy(command, &tp_res[0], 2);
			bytes_written = 2;

			if (rwnx_hw->sdiodev->tp_ctrl.set_level != 0) {
				spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
				rwnx_hw->sdiodev->tp_ctrl.tm_start = 0;
				if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer))
					//del_timer_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
					timer_delete_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
				spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
			} else if (rwnx_hw->sdiodev->tp_ctrl.set_level == 0) {
				spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
				rwnx_hw->sdiodev->tp_ctrl.tm_start = 1;
				mod_timer(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer,
					  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
				spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
			}
		}
	} else {
		AICWFDBG(LOGINFO, "tp command err\n");
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_set_tmr_intval(struct rwnx_hw *rwnx_hw, int argc,
				       char *argv[], char *command)
{
	u8_l func = 0;
	int bytes_written = 0;

	if (argc < 3) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	func = (u8_l)command_strtoul(argv[1], NULL, 10);
	if (func == 1) {
		rwnx_hw->sdiodev->tp_ctrl.interval_t1 =
			command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGDEBUG, "set tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t1, 4);
		bytes_written = 4;
	} else if (func == 2) {
		rwnx_hw->sdiodev->tp_ctrl.interval_t2 =
			command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGDEBUG, "set tmr_intval_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t2, 4);
		bytes_written = 4;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_get_tmr_intval(struct rwnx_hw *rwnx_hw, int argc,
				       char *argv[], char *command)
{
	u8_l func = 0;
	int bytes_written = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	func = (u8_l)command_strtoul(argv[1], NULL, 10);
	if (func == 1) {
		AICWFDBG(LOGDEBUG, "get tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t1, 4);
		bytes_written = 4;
	} else if (func == 2) {
		AICWFDBG(LOGDEBUG, "get tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t2, 4);
		bytes_written = 4;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_temp_get(struct rwnx_hw *rwnx_hw, int argc,
				 char *argv[], char *command)
{
	int bytes_written = 0;
	struct mm_set_vendor_swconfig_cfm tp_cfm;

	if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer)) {
		if (jiffies_to_msecs(jiffies - rwnx_hw->started_jiffies) < 5000) {
			AICWFDBG(LOGINFO, "tp_get temp_1: %d\n", rwnx_hw->temp);
			memcpy(command, &rwnx_hw->temp, 1);
		} else {
			if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
				return -1;
			AICWFDBG(LOGINFO, "tp_get temp_2: %d\n",
				 tp_cfm.temp_comp_get_cfm.degree);
			rwnx_hw->sdiodev->tp_ctrl.cur_temp =
				tp_cfm.temp_comp_get_cfm.degree;
			memcpy(command, &tp_cfm.temp_comp_get_cfm.degree, 1);
		}
	} else {
		if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
			return -1;
		AICWFDBG(LOGINFO, "tp_get temp_3: %d\n",
			 tp_cfm.temp_comp_get_cfm.degree);
		memcpy(command, &tp_cfm.temp_comp_get_cfm.degree, 1);
	}
	bytes_written = 1;

	return bytes_written;
}

static int aic_priv_cmd_tp_thd_set(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	u8_l func = 0;
	int bytes_written = 0;

	if (argc < 3) {
		AICWFDBG(LOGERROR, "%s param err\n", __func__);
		return -1;
	}
	func = (u8_l)command_strtoul(argv[1], NULL, 10);

	if (func == 1) {
		rwnx_hw->sdiodev->tp_ctrl.tp_thd_1 = command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGINFO, "set tp_thd_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_1, 1);
		bytes_written = 1;
	} else if (func == 2) {
		rwnx_hw->sdiodev->tp_ctrl.tp_thd_2 = command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGINFO, "set tp_thd_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_2, 1);
		bytes_written = 1;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}
	return bytes_written;
}

static int aic_priv_cmd_tp_thd_get(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	u8_l func = 0;
	int bytes_written = 0;

	if (argc < 2) {
		AICWFDBG(LOGERROR, "%s param err\n", __func__);
		return -1;
	}
	func = (u8_l)command_strtoul(argv[1], NULL, 10);

	if (func == 1) {
		AICWFDBG(LOGINFO, "get tp_thd_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_1, 1);
		bytes_written = 1;
	} else if (func == 2) {
		AICWFDBG(LOGINFO, "set tp_thd_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_2, 1);
		bytes_written = 1;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}
	return bytes_written;
}

#endif
#ifdef CONFIG_AIC8800_GENL
static int aic_priv_cmd_pktft_set(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	int ret = 0;

	if (argc < 6) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	ret = cmd_pktfilter_set(rwnx_hw, &argv[1], argc - 1, NULL, NULL);
	return ret;
}

static int aic_priv_cmd_pktft_del(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	int ret = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	ret = cmd_pktfilter_del(rwnx_hw, &argv[1], argc - 1, NULL, NULL);
	return ret;
}

static int aic_priv_cmd_pktft_delall(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	int ret = 0;

	ret = cmd_pktfilter_delall(rwnx_hw, argv, 0, NULL, NULL);
	return ret;
}

static int aic_priv_cmd_pktft_enable(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	int ret;

	if (argc < 3) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	ret = cmd_pktfilter_enable(rwnx_hw, &argv[1], argc - 1, NULL, NULL);
	return ret;
}

static int aic_priv_cmd_pktft_list(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	unsigned long r_len = 0;
	int bytes_written = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	cmd_pktfilter_list(rwnx_hw, &argv[1], argc - 1, command, &r_len);
	bytes_written = r_len;

	return bytes_written;
}
#endif

static int aic_priv_cmd_set_suspend(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8_l func = 0;
	s8_l err = -1;
	int ret = 0;

	if (argc < 2 || rwnx_hw->testmode != 0) {
		AICWFDBG(LOGERROR, "%s param err or in rf_test mode\n", __func__);
		return -1;
	}
	func = (u8_l)command_strtoul(argv[1], NULL, 10);

#ifndef CONFIG_AIC8800_AUTO_POWERSAVE
	if (func == 0) {
		AICWFDBG(LOGINFO, "priv_cmd suspend 0\n");
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 0, 1);
	} else if (func == 1) {
		AICWFDBG(LOGINFO, "priv_cmd suspend 1\n");
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 1, 0);
		if (rwnx_hw->scan_request && rwnx_hw->scanning) {
			pr_info("AICWF enter suspend, stop scan\n");
			ret = rwnx_send_scanu_cancel_req(rwnx_hw, NULL);
			/* make sure fw take effect */
			msleep(50);
			if (ret) {
				pr_info("AICWF %s scanu_cancel fail\n", __func__);
				return ret;
			}
		}
	} else {
		AICWFDBG(LOGERROR, "param err\n");
		ret = -1;
	}
#else
	func = 15;
#endif

	if (ret == 0) {
		memcpy(command, &func, 1);
		return 1;
	}
	memcpy(command, &err, 1);
	return 1;
}

static int aic_priv_cmd_get_version(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	int bytes_written = 0;

	AICWFDBG(LOGINFO, "Firmware Version: %s\n", rwnx_hw->fw_version);
	memcpy(command, rwnx_hw->fw_version, 32);
	bytes_written = 32;
	return bytes_written;
}

static int aic_priv_cmd_get_link_status(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_vif *rwnx_vif_st = NULL;
	struct wf_bss_info bi;

	bi.length = 0;
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION))
			rwnx_vif_st = rwnx_vif;
	}
	if (!rwnx_vif_st) {
		AICWFDBG(LOGINFO, "rwnx_vif_st is NULL\n");
		memcpy(command, &bi, 4);
		return 4;
	}
	if (atomic_read(&rwnx_vif_st->drv_conn_state) !=
		(int)RWNX_DRV_STATUS_CONNECTED) {
		AICWFDBG(LOGINFO, "rwnx_vif_st is not conncet\n");
		memcpy(command, &bi, 4);
		return 4;
	}

	bi.ssid_len = rwnx_vif_st->sta.ssid_len;
	bi.band = rwnx_vif_st->sta.ap->band;
	bi.width = rwnx_vif_st->sta.ap->width;
	bi.center_freq = rwnx_vif_st->sta.ap->center_freq;
	bi.center_freq1 = rwnx_vif_st->sta.ap->center_freq1;
	bi.center_freq2 = rwnx_vif_st->sta.ap->center_freq2;
	bi.ht = rwnx_vif_st->sta.ap->ht;
	bi.vht = rwnx_vif_st->sta.ap->vht;
	bi.chan = ieee80211_frequency_to_channel(bi.center_freq);
	memcpy(bi.bssid, rwnx_vif_st->sta.bssid, ETH_ALEN);
	memset(bi.ssid, 0, sizeof(bi.ssid));
	memcpy(bi.ssid, rwnx_vif_st->sta.ssid, rwnx_vif_st->sta.ssid_len);
	bi.length = sizeof(bi);

	memcpy(command, &bi, bi.length);
	return bi.length;
}

static int aic_priv_cmd_get_auth_type(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	struct rwnx_vif *rwnx_vif = NULL;
	s32_l val = -1;

	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) &&
			atomic_read(&rwnx_vif->drv_conn_state) ==
				(int)RWNX_DRV_STATUS_CONNECTED)
			val = rwnx_vif->sta.auth_type;
	}

	memcpy(command, &val, 4);
	return 4;
}

static int aic_priv_cmd_help(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
			     char *command)
{
	print_help(argc > 0 ? argv[0] : NULL);
	return 0;
}

struct aic_priv_cmd {
	const char *cmd;
	int (*handler)(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
		       char *command);
	const char *usage;
};

static const struct aic_priv_cmd aic_priv_commands[] = {
	{"set_tx", aic_priv_cmd_set_tx,
	 "<chan> <bw> <mode> <rate> <length> <interval>"},
	{"set_txstop", aic_priv_cmd_set_txstop, "= stop tx "},
	{"set_rx", aic_priv_cmd_set_rx, "<chan_num> <bw> "},
	{"get_rx_result", aic_priv_cmd_get_rx_result,
	 "= display rx fcsok/total pkt num"},
	{"set_rxstop", aic_priv_cmd_set_rxstop, "= stop rx "},
	{"set_txtone", aic_priv_cmd_set_tx_tone, "<val> val = 0/off"},
	{"set_rx_meter", aic_priv_cmd_set_rx_meter, "= set rx meter "},
	{"set_power", aic_priv_cmd_set_set_power, "<dec val> "},
	{"set_xtal_cap", aic_priv_cmd_set_xtal_cap, "<dec val> [0 ~ 31]"},
	{"set_xtal_cap_fine", aic_priv_cmd_set_xtal_cap_fine, "<dec val> [0 ~ 63]"},
	{"get_efuse_block", aic_priv_cmd_get_efuse_block, "<val>"},
	{"set_freq_cal", aic_priv_cmd_set_freq_cal, "<hex val>"},
	{"set_freq_cal_fine", aic_priv_cmd_set_freq_cal_fine, "<hex val>"},
	{"get_freq_cal", aic_priv_cmd_get_freq_cal, "= display cap & cap fine"},
	{"set_mac_addr", aic_priv_cmd_set_mac_addr,
	 "= write WiFi MAC into efuse or flash is limited to a maximum of two times"
	 },
	{"get_mac_addr", aic_priv_cmd_get_mac_addr,
	 "= display WiFi MAC stored in efuse or flash"},
	{"set_bt_mac_addr", aic_priv_cmd_set_bt_mac_addr,
	 "= write BT MAC into efuse or flash is limited to a maximum of two times"},
	{"get_bt_mac_addr", aic_priv_cmd_get_bt_mac_addr,
	 "= display BT MAC stored in efuse or flash"},
	{"set_vendor_info", aic_priv_cmd_set_vendor_info,
	 "= write vendor info into efuse or flash is allowed only once"},
	{"get_vendor_info", aic_priv_cmd_get_vendor_info,
	 "= display vendor info stored in efuse or flash"},
	{"rdwr_pwrmm", aic_priv_cmd_rdwr_pwrmm,
	 "<val> = 0/rdwr_pwrlvl, 1/set_power = read/write txpwr manul mode"},
	{"rdwr_pwridx", aic_priv_cmd_rdwr_pwridx, "<band> <mod> <idx>"},
	{"rdwr_pwrlvl", aic_priv_cmd_rdwr_pwrlvl, "<band> <mod> <idx>"},
	{"rdwr_pwrofst", aic_priv_cmd_rdwr_pwrofst, "<band> <rate> <ch> <ofst>"},
	{"rdwr_pwrofstfine", aic_priv_cmd_rdwr_pwrofstfine,
	 "<band> <rate> <ch> <ofstfine>"},
	{"rdwr_drvibit", aic_priv_cmd_rdwr_drvibit,
	 "<func> <val> read/write 8800D pa drvibit"},
	{"set_cal_xtal", aic_priv_cmd_set_cal_xtal, "= set cal xtal"},
	{"get_cal_xtal_res", aic_priv_cmd_get_cal_xtal_res,
	 "= get cal xtal result cap & cap_fine"},
	{"set_cob_cal", aic_priv_cmd_set_cob_cal,
	 "<dutid> <chip_num> <disxtal> = dut cob test"},
	{"get_cob_cal_res", aic_priv_cmd_get_cob_cal_res, "= get cob cal result"},
	{"do_cob_test", aic_priv_cmd_do_cob_test,
	 "<func> = 0/xtal, 1/dis_xtal, 2/only_xtal"},
	{"rdwr_efuse_pwrofst", aic_priv_cmd_rdwr_efuse_pwrofst,
	 "<band> <rate> <ch> <ofst> limited to a maximum of two times"},
	{"rdwr_efuse_pwrofstfine", aic_priv_cmd_rdwr_efuse_pwrofstfine,
	 "<band> <rate> <ch> <ofstfine> limited to a maximum of two times"},
	{"rdwr_efuse_drvibit", aic_priv_cmd_rdwr_efuse_drvibit,
	 "<func> <val> = read/write 8800D efuse pa drvibitis allowed only once"},
	{"rdwr_efuse_usrdata", aic_priv_cmd_rdwr_efuse_usrdata,
	 "<func> <val> = read/write efuse usrdata"},
	{"rdwr_efuse_sdiocfg", aic_priv_cmd_rdwr_efuse_sdiocfg,
	 "<func> <val> = read/write sdiocfg_bit into efuse"},
	{"rdwr_efuse_usbvidpid", aic_priv_cmd_rdwr_efuse_usbvidpid,
	 "<func> <val> = read/write usb vid/pid into efuse"},
	{"rdwr_efuse_he_off", aic_priv_cmd_rdwr_efuse_he_off,
	 "<func> = read/write he_off into efuse"},
	{"set_papr", aic_priv_cmd_set_papr,
	 "<val> = configure papr filter to optimize sideband suppression"},
	{"set_notch", aic_priv_cmd_set_notch,
	 "<val> = configure filter to optimize sideband suppression"},
	{"set_srrc", aic_priv_cmd_set_srrc,
	 "<func> = disable/enable sideband suppression for SRRC"},
	{"set_fss", aic_priv_cmd_set_fss,
	 "<func> = disable/enable treatment of spurious emissions and burrs"},
	{"set_usb_off", aic_priv_cmd_set_usb_off,
	 "= off usb configure before usb disconnect"},
	{"set_pll_test", aic_priv_cmd_set_pll_test,
	 "<func> <freq> <tx_pwr> = use pll test to measure saturation power"},
	{"get_txpwr", aic_priv_cmd_get_txpwr, "= get userconfig max txpwr"},
	{"set_txpwr_loss", aic_priv_cmd_set_txpwr_loss,
	 "<val> = txpwr will change ,val can be negative"},
	{"rdwr_pwradd2x", aic_priv_cmd_rdwr_pwradd2x,
	  "a value is added for both 2.4G and 5G to achieve overall power adjustment of the band"},
	{"rdwr_efuse_pwradd2x", aic_priv_cmd_rdwr_efuse_pwradd2x,
	  "add power offset for both 2.4G and 5G, write to efuse"},

	/* The following is not an RF cmd */
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
	{"country_set", aic_priv_cmd_country_set, "<ccode>"},
	{"country_get", aic_priv_cmd_country_get, "no param"},
#endif
#ifdef CONFIG_AIC8800_TEMP_CONTROL
	{"TEMP_CTRL_SW", aic_priv_cmd_temp_ctrl_sw, "<val> 1--open, 0--close"},
	{"TEMP_CTRL_SET_GET", aic_priv_cmd_temp_sget,
	 "<option> <val> option--0-get,1-set; val--0/1/2"},
	{"SET_TMR_INTVAL", aic_priv_cmd_set_tmr_intval,
	 "<index> <time> index--0/1, time ms"},
	{"GET_TMR_INTVAL", aic_priv_cmd_get_tmr_intval, "<index> index--0/1"},
	{"TEMP_GET", aic_priv_cmd_temp_get, "no param"},
	{"TEMP_THRESHOLD_SET", aic_priv_cmd_tp_thd_set,
	 "<index> <val> index--0/1, val--degree centigrade"},
	{"TEMP_THRESHOLD_GET", aic_priv_cmd_tp_thd_get, "<index> inddex--0/1"},
#endif
#ifdef CONFIG_AIC8800_GENL
	{"pkt_filter_set", aic_priv_cmd_pktft_set,
	 "<code> <offset> <length> <mask> <pattern>"},
	{"pkt_filter_del", aic_priv_cmd_pktft_del, "<id>"},
	{"pkt_filter_delall", aic_priv_cmd_pktft_delall, "no param"},
	{"pkt_filter_enable", aic_priv_cmd_pktft_enable, "<id> <val>"},
	{"pkt_filter_list", aic_priv_cmd_pktft_list, "<val>"},
#endif
	{"set_suspend", aic_priv_cmd_set_suspend,
	 "<mode>, 1/0----enter/exit lp_level"},
	{"get_version", aic_priv_cmd_get_version, "no param, get fw version"},
	{"status", aic_priv_cmd_get_link_status, "no param, get link status"},
	{"wpa_auth", aic_priv_cmd_get_auth_type,
	 "no param, get AuthenticationType"},

	// Reserve for new aic_priv_cmd.
	{"help", aic_priv_cmd_help, "= show usage help"},
	{NULL, NULL, NULL}

};

/*
 * Prints command usage, lines are padded with the specified string.
 */
static void print_help(const char *cmd)
{
	int n;

	pr_info("commands:\n");
	for (n = 0; aic_priv_commands[n].cmd; n++) {
		if (cmd)
			pr_info("%s %s\n", aic_priv_commands[n].cmd,
				aic_priv_commands[n].usage);
	}
}

int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
	const struct aic_priv_cmd *cmd, *match = NULL;
	int count;
	int bytes_written = 0;
	char **argv = NULL;
	int argc;
	struct rwnx_vif *vif =
		container_of(net->ieee80211_ptr, struct rwnx_vif, wdev);
	struct rwnx_hw *p_rwnx_hw = vif->rwnx_hw;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	argv = kzalloc((CMD_MAXARGS + 1) * sizeof(char *), GFP_KERNEL);
	if (!argv) {
		AICWFDBG(LOGERROR, "%s alloc argv fail\n", __func__);
		return -ENOMEM;
	}

	argc = parse_line(command, argv);
	if (argc == 0 || argc > CMD_MAXARGS) {
		AICWFDBG(LOGERROR, "%s params error, count: %d\n", __func__, argc);
		kfree(argv);
		return -EINVAL;
	}

	count = 0;
	cmd = aic_priv_commands;
	while (cmd->cmd) {
		if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0 &&
		    strncasecmp(cmd->cmd, argv[0], strlen(cmd->cmd)) == 0) {
			match = cmd;
			if (strcasecmp(cmd->cmd, argv[0]) == 0) {
				/* we have an exact match */
				count = 1;
				break;
			}
			count++;
		}
		cmd++;
	}

	if (count > 1) {
		AICWFDBG(LOGINFO,
			 "Ambiguous command '%s'; possible commands:", argv[0]);
		cmd = aic_priv_commands;
		while (cmd->cmd) {
			if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0)
				AICWFDBG(LOGINFO, " %s", cmd->cmd);
			cmd++;
		}
		AICWFDBG(LOGINFO, "\n");
	} else if (count == 0) {
		AICWFDBG(LOGERROR, "Unknown command '%s'\n", argv[0]);
		kfree(argv);
		return -EINVAL;
	}
	AICWFDBG(LOGINFO, "match %s", match->cmd);
	bytes_written = match->handler(p_rwnx_hw, argc, &argv[0], command);

	if (bytes_written < 0)
		AICWFDBG(LOGERROR, "wrong param\n");

	kfree(argv);
	return bytes_written;
}

#define RWNX_COUNTRY_CODE_LEN 2
#define CMD_SET_COUNTRY       "COUNTRY"
#define CMD_SET_VENDOR_EX_IE  "SET_VENDOR_EX_IE"
#define CMD_SET_AP_WPS_P2P_IE "SET_AP_WPS_P2P_IE"
#define CMD_SETSUSPENDMODE    "SETSUSPENDMODE"

#ifdef CONFIG_AIC8800_SET_VENDOR_EXTENSION_IE

static void set_vendor_extension_ie(char *command)
{
	char databyte[3] = {0x00, 0x00, 0x00};
	int skip = strlen(CMD_SET_VENDOR_EX_IE) + 1;
	int command_index = skip;
	int data_index = 0;

	memset(vendor_extension_data, 0, 256);
	vendor_extension_len = 0;
	memcpy(databyte, command + command_index, 2);
	vendor_extension_len = command_strtoul(databyte, NULL, 16);
	pr_info("%s len:%d \r\n", __func__, vendor_extension_len);

	// parser command and save data in vendor_extension_data
	for (data_index = 0; data_index < vendor_extension_len; data_index++) {
		command_index = command_index + 3;
		memcpy(databyte, command + command_index, 2);
		vendor_extension_data[data_index] = command_strtoul(databyte, NULL, 16);
	}
}
#endif // CONFIG_AIC8800_SET_VENDOR_EXTENSION_IE

int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
#define PRIVATE_COMMAND_MAX_LEN 8192
#define PRIVATE_COMMAND_DEF_LEN 6144

	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	struct android_wifi_priv_cmd priv_cmd;
	int buf_size = 0;
#ifdef ANDROID_PLATFORM
	struct rwnx_vif *vif = netdev_priv(net);
	int skip = 0;
	char *country = NULL;
	int setsusp_mode;
	const struct ieee80211_regdomain *regdomain;
#endif

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		struct compat_android_wifi_priv_cmd compat_priv_cmd;

		if (copy_from_user(&compat_priv_cmd, ifr->ifr_data,
				   sizeof(struct compat_android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
		priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
		priv_cmd.used_len = compat_priv_cmd.used_len;
		priv_cmd.total_len = compat_priv_cmd.total_len;
	} else {
#endif /* CONFIG_COMPAT */
		if (copy_from_user(&priv_cmd, ifr->ifr_data,
				   sizeof(struct android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
#ifdef CONFIG_COMPAT
	}
#endif /* CONFIG_COMPAT */
	if (priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN ||
	    priv_cmd.total_len < 0) {
		pr_err("%s: buf length invalid:%d\n", __func__, priv_cmd.total_len);
		ret = -EINVAL;
		goto exit;
	}

	buf_size = max(priv_cmd.total_len, PRIVATE_COMMAND_DEF_LEN);
	command =
		kmalloc((buf_size + 1), GFP_KERNEL);
	if (!command) {
		pr_err("%s: failed to allocate memory\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';

	AICWFDBG(LOGINFO, "%s: Android private cmd \"%s\" on %s\n", __func__,
		 command, ifr->ifr_name);

#ifdef ANDROID_PLATFORM
	if (!strncasecmp(command, CMD_SET_COUNTRY, strlen(CMD_SET_COUNTRY))) {
		skip = strlen(CMD_SET_COUNTRY) + 1;
		country = command + skip;
		if (!country || strlen(country) < RWNX_COUNTRY_CODE_LEN) {
			pr_err("%s: invalid country code\n", __func__);
			ret = -EINVAL;
			goto exit;
		}

		AICWFDBG(LOGINFO, "%s country code:%c%c\n", __func__,
			 toupper(country[0]), toupper(country[1]));
		regdomain = get_regdomain_from_rwnx_db(vif->rwnx_hw->wiphy, country);
		ret = rwnx_regulatory_set_wiphy_regd(vif->rwnx_hw->wiphy,
						     regdomain);
		if (ret)
			pr_warn("regulatory_set_wiphy_regd fail \r\n");
#ifdef CONFIG_AIC8800_POWER_LIMIT
		aic_chip_powerlimit_load(vif->rwnx_hw);
		if (!rwnx_hw->testmode)
			rwnx_send_me_chan_config_req(vif->rwnx_hw);
#endif
	}
#ifdef CONFIG_AIC8800_SET_VENDOR_EXTENSION_IE
	else if (!strncasecmp(command, CMD_SET_VENDOR_EX_IE,
			      strlen(CMD_SET_VENDOR_EX_IE))) {
		set_vendor_extension_ie(command);
	}
#endif // CONFIG_AIC8800_SET_VENDOR_EXTENSION_IE
	else if (!strncasecmp(command, CMD_SET_AP_WPS_P2P_IE,
			      strlen(CMD_SET_AP_WPS_P2P_IE))) {
		ret = 0;
		goto exit;
	} else if (!strncasecmp(command, CMD_SETSUSPENDMODE,
							strlen(CMD_SETSUSPENDMODE))) {
#ifdef AICWF_SDIO_SUPPORT
#if defined(CONFIG_GPIO_WAKEUP) && !defined(CONFIG_AIC8800_AUTO_POWERSAVE)
		skip = strlen(CMD_SETSUSPENDMODE) + 1;
		setsusp_mode = command_strtoul(command + skip, NULL, 10);
#ifdef AICWF_LATENCY_MODE
		struct rwnx_hw *hw = rwnx_platform_get_hw(g_rwnx_plat);
		if (setsusp_mode)
			rwnx_send_me_set_lp_level(hw, setsusp_mode, 0);
		else
			rwnx_send_me_set_lp_level(hw, 1, 1);
#else
		struct rwnx_hw *hw = rwnx_platform_get_hw(g_rwnx_plat);
		rwnx_send_me_set_lp_level(hw,
					  setsusp_mode, !setsusp_mode);
#endif

	AICWFDBG(LOGINFO, "set suspend mode %d\n", setsusp_mode);
#endif // CONFIG_GPIO_WAKEUP
#endif
		goto exit;
	}
#endif // Handle Android command

	bytes_written = handle_private_cmd(net, command, priv_cmd.total_len);
	if (bytes_written >= 0) {
		if (bytes_written == 0 && priv_cmd.total_len > 0)
			command[0] = '\0';
		if (bytes_written >= priv_cmd.total_len) {
			pr_info("%s: err. bytes_written:%d >= buf_size:%d\n", __func__,
				bytes_written, buf_size);
			goto exit;
		}
		bytes_written++;
		priv_cmd.used_len = bytes_written;
		if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
			pr_err("%s: failed to copy data to user buffer\n", __func__);
			ret = -EFAULT;
		}
	} else {
		/* Propagate the error */
		ret = bytes_written;
	}

exit:
	kfree(command);
	return ret;
}
