/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AIC8800DC_COMPAT_H_
#define _AIC8800DC_COMPAT_H_

#include "aicsdio.h"
typedef u32 (*array2_tbl_t)[2];
typedef u32 (*array3_tbl_t)[3];

typedef u8 u8_l;
typedef s8 s8_l;
typedef u16 u16_l;
typedef s16 s16_l;
typedef u32 u32_l;
typedef s32 s32_l;
typedef u64 u64_l;

extern u8 chip_sub_id;
extern u8 chip_mcu_id;
#define FW_PATH_MAX_LEN 200

void aicwf_patch_config_8800dc(struct aic_sdio_dev *rwnx_hw);
void system_config_8800dc(struct aic_sdio_dev *rwnx_hw);
int rwnx_plat_patch_load_8800dc(struct aic_sdio_dev *sdiodev);
int aicwf_misc_ram_init_8800dc(struct aic_sdio_dev *sdiodev);
int start_from_bootrom_8800DC(struct aic_sdio_dev *sdiodev);

#ifdef CONFIG_DPD
int aicwf_dpd_calib_8800dc(struct aic_sdio_dev *sdiodev,
			   struct rf_misc_ram_lite_t *dpd_res);
int aicwf_dpd_result_apply_8800dc(struct aic_sdio_dev *sdiodev,
				  struct rf_misc_ram_lite_t *dpd_res);
#ifndef CONFIG_FORCE_DPD_CALIB
int aicwf_dpd_result_load_8800dc(struct aic_sdio_dev *sdiodev,
				 struct rf_misc_ram_lite_t *dpd_res);
#endif /* !CONFIG_FORCE_DPD_CALIB */
#endif

int set_bbpll_config(struct aic_sdio_dev *rwnx_hw);

#endif
