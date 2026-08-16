// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_8800d80.c
 * @brief Chip operations for AIC8800D80
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include "aic_chip_ops.h"

#include "aic8800d80_compat.h"
#include "aic_bsp_driver.h"

static int aic8800d80_wifi_init(struct aic_sdio_dev *sdiodev, int testmode)
{
	const char *fw_path = aicbsp_firmware_list[aicbsp_info.cpmode].wl_fw;

	aicwf_sdio_wakeup(sdiodev);

	if (rwnx_plat_bin_fw_upload(sdiodev, RAM_FMAC_FW_ADDR,
				    fw_path)) {
		pr_err("aicbsp 8800d80 download wifi fw fail\n");
		return -1;
	}

	if (aicwifi_patch_config_8800d80(sdiodev)) {
		pr_err("aicbsp aicwifi_patch_config_8800d80 fail\n");
		return -1;
	}

	if (aicwifi_sys_config_8800d80(sdiodev)) {
		pr_err("aicbsp aicwifi_patch_config_8800d80 fail\n");
		return -1;
	}

	if (aicwifi_start_from_bootrom(sdiodev)) {
		pr_err("aicbsp 8800d80 wifi start fail\n");
		return -1;
	}
	return 0;
}

static int aic8800d80_set_patch_info(struct aic_sdio_dev *sdiodev,
				     struct aicbt_patch_info_t *patch_info,
				     struct aicbsp_info_t *aicbsp_info,
				     struct aicbt_patch_table *head)
{
	if (aicbsp_info->chip_rev == CHIP_REV_U01) {
		patch_info->addr_adid = FW_RAM_ADID_BASE_ADDR_8800D80;
		patch_info->addr_patch = FW_RAM_PATCH_BASE_ADDR_8800D80;
	} else if (aicbsp_info->chip_rev == CHIP_REV_U02 ||
				aicbsp_info->chip_rev == CHIP_REV_U03) {
		patch_info->addr_adid = FW_RAM_ADID_BASE_ADDR_8800D80_U02;
		patch_info->addr_patch = FW_RAM_PATCH_BASE_ADDR_8800D80_U02;
	}
	aicbt_patch_info_unpack(patch_info, head);
	if (patch_info->info_len == 0) {
		pr_err("aicbsp %s, aicbt_patch_info_unpack fail\n", __func__);
		return -1;
	}

	return 0;
}

static int aic8800d80_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
				     struct aicbsp_info_t *aicbsp_info,
				     const struct aicbsp_firmware **aicbsp_firmware_list)
{
	u32 mem_addr;
	struct dbg_mem_read_cfm rd_mem_addr_cfm;
	u8 is_chip_id_h = 0;
	int ret;

	aicwf_sdio_wakeup(sdiodev);

	mem_addr = 0x40500000;

	ret = rwnx_send_dbg_mem_read_req(sdiodev, mem_addr, &rd_mem_addr_cfm);
	if (ret) {
		pr_warn("aicbsp: 0x40500000 read failed (%d), defaulting to AIC8800D80 U02\n", ret);
		aicbsp_info->chip_rev = CHIP_REV_U02;
		is_chip_id_h = 0;
	} else {
		aicbsp_info->chip_rev = (u8)((rd_mem_addr_cfm.memdata >> 16) & 0x3F);
		is_chip_id_h = (u8)(((rd_mem_addr_cfm.memdata >> 16) & 0xC0) == 0xC0);
	}
	*btenable = 1;
	if (is_chip_id_h) {
		AICWFDBG(LOGINFO, "IS_CHIP_ID_H\n");
		*aicbsp_firmware_list = fw_8800d80_h_u02;
	} else {
		if (aicbsp_info->chip_rev == CHIP_REV_U01)
			*aicbsp_firmware_list = fw_8800d80_u01;
		if (aicbsp_info->chip_rev == CHIP_REV_U02 ||
		    aicbsp_info->chip_rev == CHIP_REV_U03)
			*aicbsp_firmware_list = fw_8800d80_u02;
	}
	if (aicbsp_system_config_8800d80(sdiodev))
		return -1;

	return 0;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8800d80_ops = {
	.name						= "AIC8800D80",
	.use_func_msg				= false,
	.use_sdiov3_func			= true,
	.need_flowctrl_mask			= false,
	.wakeup_reg_val				= 0x11,
	.use_flowctrl_msg			= true,
	.use_hdr_checksum			= true,
	.need_fix_hdr_len			= false,
	.need_func0_intr			= true,
	.use_func2					= false,
	.sdio_clock					= FEATURE_SDIO_CLOCK_V3,
	.wifi_init					= aic8800d80_wifi_init,
	.set_patch_info				= aic8800d80_set_patch_info,
	.driver_fw_init				= aic8800d80_driver_fw_init,

};
