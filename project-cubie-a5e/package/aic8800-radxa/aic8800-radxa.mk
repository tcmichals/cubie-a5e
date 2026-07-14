################################################################################
#
# aic8800-radxa
#
################################################################################

AIC8800_RADXA_VERSION = main
AIC8800_RADXA_SITE = $(call github,radxa-pkg,aic8800,$(AIC8800_RADXA_VERSION))
AIC8800_RADXA_LICENSE = GPL-2.0

AIC8800_RADXA_MODULE_SUBDIRS = src/SDIO/driver_fw/driver/aic8800

AIC8800_RADXA_MODULE_MAKE_OPTS = \
	KDIR=$(LINUX_DIR) \
	KSRC=$(LINUX_DIR) \
	ARCH=arm64 \
	CROSS_COMPILE=$(TARGET_CROSS) \
	CONFIG_AIC8800_WLAN_SUPPORT=m \
	CONFIG_AIC_WLAN_SUPPORT=m \
	CONFIG_AIC8800_BTLPM_SUPPORT=m

define AIC8800_RADXA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/aic8800_fdrv.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/aic8800_fdrv_radxa.ko
	$(INSTALL) -D -m 0644 $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/aic8800_bsp.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/aic8800_bsp_radxa.ko
	$(INSTALL) -D -m 0644 $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_btlpm/aic8800_btlpm.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/aic8800_btlpm_radxa.ko
endef

define AIC8800_RADXA_REMOVE_MODULE_IMPORT_NS
	$(SED) '/MODULE_IMPORT_NS/d' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_main.c
	$(SED) '/MODULE_IMPORT_NS/d' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_platform.c
	$(SED) '/MODULE_IMPORT_NS/d' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/aic_bsp_driver.c
	$(SED) 's/in_irq()/0/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_msg_tx.c
	$(SED) 's/in_softirq()/0/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_msg_tx.c
	$(SED) 's/in_atomic()/0/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_msg_tx.c
	$(SED) 's/add_if_req_param->p2p = true;/add_if_req_param->p2p = true; fallthrough;/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_msg_tx.c
	$(SED) 's/<linux\/of_gpio.h>/<linux\/gpio\/consumer.h>/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_btlpm/rfkill.c
	$(SED) 's/#include "rwnx_sys_arch.h"/#include "rwnx_sys_arch.h"\\n#include <linux\/vmalloc.h>/g' $(@D)/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/aic8800d80n_compat.c
endef
AIC8800_RADXA_POST_PATCH_HOOKS += AIC8800_RADXA_REMOVE_MODULE_IMPORT_NS

$(eval $(kernel-module))
$(eval $(generic-package))
