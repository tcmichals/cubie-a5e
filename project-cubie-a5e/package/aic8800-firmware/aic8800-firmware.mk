################################################################################
#
# aic8800-firmware
#
################################################################################

AIC8800_FIRMWARE_VERSION = master
AIC8800_FIRMWARE_SITE = $(call github,radxa-pkg,aic8800,$(AIC8800_FIRMWARE_VERSION))
AIC8800_FIRMWARE_LICENSE = proprietary

define AIC8800_FIRMWARE_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/lib/firmware
	cp -r $(@D)/src/SDIO/driver_fw/fw/* $(TARGET_DIR)/lib/firmware/
	mkdir -p $(TARGET_DIR)/lib/firmware/aic8800
	cp -r $(@D)/src/SDIO/driver_fw/fw/aic8800D80/* $(TARGET_DIR)/lib/firmware/aic8800/ 2>/dev/null || true
endef

$(eval $(generic-package))
