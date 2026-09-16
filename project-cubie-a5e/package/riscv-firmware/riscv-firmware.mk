################################################################################
#
# riscv-firmware
#
################################################################################

RISCV_FIRMWARE_VERSION = 1.0
RISCV_FIRMWARE_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../firmware
RISCV_FIRMWARE_SITE_METHOD = local

define RISCV_FIRMWARE_BUILD_CMDS
	if [ -f $(@D)/e907-riscv/apps/Makefile ]; then \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/e907-riscv/apps clean && \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/e907-riscv/apps all HOST_CXX="$(TARGET_CXX)"; \
	fi
	$(TARGET_CC) $(TARGET_CFLAGS) $(@D)/e907-riscv/tools/riscv-load.c -o $(@D)/e907-riscv/tools/riscv-load
endef

define RISCV_FIRMWARE_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/lib/firmware
	mkdir -p $(TARGET_DIR)/usr/bin
	mkdir -p $(TARGET_DIR)/usr/share/riscv-firmware

	# Install all compiled RISC-V firmware ELFs and binaries
	if [ -d $(@D)/e907-riscv/bin ]; then \
		for elf in $(@D)/e907-riscv/bin/*.elf; do \
			[ -f "$$elf" ] && $(INSTALL) -D -m 0644 "$$elf" $(TARGET_DIR)/lib/firmware/$$(basename "$$elf"); \
		done; \
		for bin in $(@D)/e907-riscv/bin/*.bin; do \
			[ -f "$$bin" ] && $(INSTALL) -D -m 0644 "$$bin" $(TARGET_DIR)/lib/firmware/$$(basename "$$bin"); \
		done; \
	fi

	# Default active firmware loaded at boot by /etc/init.d/S60riscv
	if [ -f $(@D)/e907-riscv/bin/testStringBinaryTrace0.elf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/e907-riscv/bin/testStringBinaryTrace0.elf $(TARGET_DIR)/lib/firmware/riscv-firmware.elf; \
		$(INSTALL) -D -m 0755 $(@D)/e907-riscv/bin/testStringBinaryTrace0.elf $(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf; \
	elif [ -f $(@D)/e907-riscv/bin/exampleRiscv.elf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/e907-riscv/bin/exampleRiscv.elf $(TARGET_DIR)/lib/firmware/riscv-firmware.elf; \
		$(INSTALL) -D -m 0755 $(@D)/e907-riscv/bin/exampleRiscv.elf $(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf; \
	fi

	# Install Linux host benchmark and communication tools
	for tool in ping_shm ping_rpmsg ping_dram; do \
		if [ -f $(@D)/e907-riscv/bin/$$tool ]; then \
			$(INSTALL) -D -m 0755 $(@D)/e907-riscv/bin/$$tool $(TARGET_DIR)/usr/bin/$$tool; \
		fi; \
	done
	$(INSTALL) -D -m 0755 $(@D)/e907-riscv/tools/riscv-load $(TARGET_DIR)/usr/bin/riscv-load
	$(INSTALL) -D -m 0755 $(@D)/e907-riscv/tools/load-riscv.sh $(TARGET_DIR)/usr/bin/load-riscv.sh
	$(INSTALL) -D -m 0755 $(@D)/e907-riscv/tools/test_riscv.py $(TARGET_DIR)/usr/bin/test_riscv.py

	# Install host companion Python trace monitor & telemetry tools
	for py_tool in monitor_trace.py fast_sram_telemetry.py; do \
		if [ -f $(@D)/e907-riscv/bin/$$py_tool ]; then \
			$(INSTALL) -D -m 0755 $(@D)/e907-riscv/bin/$$py_tool $(TARGET_DIR)/usr/bin/$$py_tool; \
		fi; \
	done
endef

RISCV_FIRMWARE_INSTALL_IMAGES = YES

define RISCV_FIRMWARE_INSTALL_IMAGES_CMDS
	if [ -d $(@D)/e907-riscv/bin ]; then \
		for elf in $(@D)/e907-riscv/bin/*.elf; do \
			[ -f "$$elf" ] && $(INSTALL) -D -m 0644 "$$elf" $(BINARIES_DIR)/$$(basename "$$elf"); \
		done; \
	fi
	if [ -f $(@D)/e907-riscv/bin/testStringBinaryTrace0.elf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/e907-riscv/bin/testStringBinaryTrace0.elf $(BINARIES_DIR)/riscv-firmware.elf; \
	elif [ -f $(@D)/e907-riscv/bin/exampleRiscv.elf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/e907-riscv/bin/exampleRiscv.elf $(BINARIES_DIR)/riscv-firmware.elf; \
	fi
endef

$(eval $(generic-package))
