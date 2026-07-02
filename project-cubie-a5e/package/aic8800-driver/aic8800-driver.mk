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
	CONFIG_AIC8800_WLAN_SUPPORT=m

$(eval $(kernel-module))
$(eval $(generic-package))
