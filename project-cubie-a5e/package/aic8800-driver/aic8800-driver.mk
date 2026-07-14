################################################################################
#
# aic8800-driver
#
################################################################################

AIC8800_DRIVER_VERSION = master
AIC8800_DRIVER_SITE = $(call github,shenmintao,aic8800d80,$(AIC8800_DRIVER_VERSION))
AIC8800_DRIVER_LICENSE = GPL-2.0

AIC8800_DRIVER_MODULE_SUBDIRS = drivers/aic8800

AIC8800_DRIVER_MODULE_MAKE_OPTS = \
	KDIR=$(LINUX_DIR) \
	KSRC=$(LINUX_DIR) \
	ARCH=arm64 \
	CROSS_COMPILE=$(TARGET_CROSS) \
	CONFIG_AIC8800_WLAN_SUPPORT=m \
	CONFIG_AIC_LOADFW_SUPPORT=m \
	CONFIG_SDIO_SUPPORT=y \
	CONFIG_USB_SUPPORT=y

define AIC8800_DRIVER_FORCE_SDIO
	$(SED) 's/^CONFIG_SDIO_SUPPORT *=.*/CONFIG_SDIO_SUPPORT =y/' $(@D)/drivers/aic8800/aic8800_fdrv/Makefile
	$(SED) 's/^CONFIG_USB_SUPPORT *=.*/CONFIG_USB_SUPPORT =y/' $(@D)/drivers/aic8800/aic8800_fdrv/Makefile
	$(SED) '/aic_priv_cmd.o/d' $(@D)/drivers/aic8800/aic8800_fdrv/Makefile
	@echo ">>> AIC8800: Forced CONFIG_SDIO_SUPPORT=y and CONFIG_USB_SUPPORT=y"
	@grep -E 'CONFIG_SDIO_SUPPORT|CONFIG_USB_SUPPORT' $(@D)/drivers/aic8800/aic8800_fdrv/Makefile
endef
AIC8800_DRIVER_POST_PATCH_HOOKS += AIC8800_DRIVER_FORCE_SDIO

$(eval $(kernel-module))
$(eval $(generic-package))
