################################################################################
#
# riscv-firmware
#
################################################################################

RISCV_FIRMWARE_VERSION = 1.0
RISCV_FIRMWARE_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../riscv-firmware
RISCV_FIRMWARE_SITE_METHOD = local

define RISCV_FIRMWARE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define RISCV_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/firmware.bin $(TARGET_DIR)/lib/firmware/riscv-firmware.bin
	$(INSTALL) -D -m 0755 $(@D)/firmware.elf $(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf
endef

$(eval $(generic-package))
