################################################################################
#
# riscv-firmware
#
################################################################################

RISCV_FIRMWARE_VERSION = 1.0
RISCV_FIRMWARE_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../riscv-firmware
RISCV_FIRMWARE_SITE_METHOD = local

define RISCV_FIRMWARE_BUILD_CMDS
	if [ -f $(@D)/apps/exampleRiscv/Makefile ]; then \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/apps/exampleRiscv clean && \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/apps/exampleRiscv all; \
	fi
	$(TARGET_CC) $(TARGET_CFLAGS) $(@D)/tools/riscv-load.c -o $(@D)/tools/riscv-load
endef

define RISCV_FIRMWARE_INSTALL_TARGET_CMDS
	if [ -f $(@D)/apps/exampleRiscv/firmware.bin ]; then \
		rm -f $(TARGET_DIR)/lib/firmware/riscv-firmware.elf; \
		$(INSTALL) -D -m 0644 $(@D)/apps/exampleRiscv/firmware.bin $(TARGET_DIR)/lib/firmware/riscv-firmware.bin; \
		$(INSTALL) -D -m 0644 $(@D)/apps/exampleRiscv/firmware.elf $(TARGET_DIR)/lib/firmware/riscv-firmware.elf; \
		$(INSTALL) -D -m 0755 $(@D)/apps/exampleRiscv/firmware.elf $(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf; \
	fi
	$(INSTALL) -D -m 0755 $(@D)/tools/riscv-load $(TARGET_DIR)/usr/bin/riscv-load
	$(INSTALL) -D -m 0755 $(@D)/tools/test_riscv.py $(TARGET_DIR)/usr/bin/test_riscv.py
	$(INSTALL) -D -m 0755 $(@D)/tools/load-riscv.sh $(TARGET_DIR)/usr/bin/load-riscv.sh
endef

RISCV_FIRMWARE_INSTALL_IMAGES = YES

define RISCV_FIRMWARE_INSTALL_IMAGES_CMDS
	if [ -f $(@D)/apps/exampleRiscv/firmware.elf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/apps/exampleRiscv/firmware.elf $(BINARIES_DIR)/riscv-firmware.elf; \
		$(INSTALL) -D -m 0644 $(@D)/apps/exampleRiscv/firmware.bin $(BINARIES_DIR)/riscv-firmware.bin; \
	fi
endef

$(eval $(generic-package))
