################################################################################
#
# sunxi-galcore
#
################################################################################

# Use the standard out-of-tree Vivante NPU kernel driver source
SUNXI_GALCORE_VERSION = 4d035200e7b15d2713d49979a1d05f201b92cf4c
SUNXI_GALCORE_SITE = $(call github,Freescale,kernel-module-imx-gpu-viv,$(SUNXI_GALCORE_VERSION))
SUNXI_GALCORE_LICENSE = GPL-2.0
SUNXI_GALCORE_LICENSE_FILES = COPYING

SUNXI_GALCORE_MODULE_MAKE_OPTS = \
	AQROOT=$(@D)/kernel-module-imx-gpu-viv-src \
	KERNEL_DIR=$(LINUX_DIR)

SUNXI_GALCORE_MODULE_SUBDIRS = kernel-module-imx-gpu-viv-src

$(eval $(kernel-module))
$(eval $(generic-package))
