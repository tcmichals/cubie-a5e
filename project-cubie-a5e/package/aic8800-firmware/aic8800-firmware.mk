################################################################################
#
# aic8800-firmware
#
################################################################################

AIC8800_FIRMWARE_VERSION = master
AIC8800_FIRMWARE_SITE = $(call github,radxa-pkg,aic8800,$(AIC8800_FIRMWARE_VERSION))
AIC8800_FIRMWARE_LICENSE = proprietary

ifeq ($(BR2_PACKAGE_AIC8800_DRIVER_USB),y)
AIC8800_FIRMWARE_SUBDIR = USB
AIC8800_FIRMWARE_DEPENDENCIES += linux

define AIC8800_FIRMWARE_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) M=$(@D)/src/USB/driver_fw/drivers/aic8800/aic_load_fw \
		ARCH=arm64 CROSS_COMPILE=$(TARGET_CROSS) modules
endef

define AIC8800_FIRMWARE_INSTALL_USB_LOADER
	mkdir -p $(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/updates/aic_load_fw
	cp $(@D)/src/USB/driver_fw/drivers/aic8800/aic_load_fw/aic_load_fw.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/updates/aic_load_fw/
endef
AIC8800_FIRMWARE_POST_INSTALL_TARGET_HOOKS += AIC8800_FIRMWARE_INSTALL_USB_LOADER
else
AIC8800_FIRMWARE_SUBDIR = SDIO
endif

define AIC8800_FIRMWARE_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/lib/firmware
	cp -r $(@D)/src/$(AIC8800_FIRMWARE_SUBDIR)/driver_fw/fw/* $(TARGET_DIR)/lib/firmware/
	mkdir -p $(TARGET_DIR)/lib/firmware/aic8800
	cp -r $(TARGET_DIR)/lib/firmware/aic8800D80/* $(TARGET_DIR)/lib/firmware/aic8800/ 2>/dev/null || true
endef

$(eval $(generic-package))
