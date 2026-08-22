################################################################################
#
# boot0-a733
#
################################################################################

BOOT0_A733_VERSION = device-a733-v1.4.6
BOOT0_A733_SITE = $(call github,radxa,allwinner-device,$(BOOT0_A733_VERSION))
BOOT0_A733_LICENSE = PROPRIETARY
BOOT0_A733_INSTALL_IMAGES = YES
BOOT0_A733_INSTALL_TARGET = NO

define BOOT0_A733_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 $(@D)/bin/boot0_sdcard_sun60iw2p1_lpddr5.bin \
		$(BINARIES_DIR)/boot0_sdcard.bin
endef

$(eval $(generic-package))
