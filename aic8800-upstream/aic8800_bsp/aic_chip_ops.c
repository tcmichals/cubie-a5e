// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_ops.c
 * @brief Wrapper function implementations for chip-specific operations.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include "aic_chip_ops.h"
#include "aicsdio.h"

int aicwf_sdio_chipmatch(u16 vid, u16 did, u16 *chipid)
{
	if (vid == SDIO_VENDOR_ID_AIC8801 && did == SDIO_DEVICE_ID_AIC8801) {
		*chipid = PRODUCT_ID_AIC8801;
		AICWFDBG(LOGINFO, "%s USE AIC8801\r\n", __func__);
		return 0;
	} else if (vid == SDIO_VENDOR_ID_AIC8800DC &&
		   did == SDIO_DEVICE_ID_AIC8800DC) {
		*chipid = PRODUCT_ID_AIC8800DC;
		AICWFDBG(LOGINFO, "%s USE AIC8800DC\r\n", __func__);
		return 0;
	} else if (vid == SDIO_VENDOR_ID_AIC8800D80 &&
		   did == SDIO_DEVICE_ID_AIC8800D80) {
		*chipid = PRODUCT_ID_AIC8800D80;
		AICWFDBG(LOGINFO, "%s USE AIC8800D80\r\n", __func__);
		return 0;
	}
	return -ENODEV;
}

/*========== Chip ops wrappers ========== */
int aic_chip_wifi_init(struct aic_sdio_dev *sdiodev, int testmode)
{
	if (sdiodev->chip_ops && sdiodev->chip_ops->wifi_init)
		return sdiodev->chip_ops->wifi_init(sdiodev, testmode);
	return 0;
}

int aic_chip_set_patch_info(struct aic_sdio_dev *sdiodev,
			    struct aicbt_patch_info_t *patch_info,
			    struct aicbsp_info_t *aicbsp_info,
			    struct aicbt_patch_table *head)
{
	if (sdiodev->chip_ops && sdiodev->chip_ops->set_patch_info)
		return sdiodev->chip_ops->set_patch_info(sdiodev, patch_info,
							 aicbsp_info, head);
	return 0;
}

int aic_chip_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
			    struct aicbsp_info_t *aicbsp_info,
			    const struct aicbsp_firmware **aicbsp_firmware_list)
{
	if (sdiodev->chip_ops && sdiodev->chip_ops->driver_fw_init)
		return sdiodev->chip_ops->driver_fw_init(sdiodev, btenable,
							 aicbsp_info,
							 aicbsp_firmware_list);
	return 0;
}

const struct aic_chip_ops *aic_chip_ops_select(u16 chipid)
{
	switch (chipid) {
	case PRODUCT_ID_AIC8801:
		return &aic_chip_aic8801_ops;
	case PRODUCT_ID_AIC8800DC:
	case PRODUCT_ID_AIC8800DW:
		return &aic_chip_aic8800dc_ops;
	case PRODUCT_ID_AIC8800D80:
		return &aic_chip_aic8800d80_ops;
	default:
		return NULL;
	}
}
