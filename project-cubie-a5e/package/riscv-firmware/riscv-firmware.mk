################################################################################
#
# riscv-firmware
#
################################################################################

RISCV_FIRMWARE_VERSION = 1.0
RISCV_FIRMWARE_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../firmware
RISCV_FIRMWARE_SITE_METHOD = local

ifeq ($(BR2_PACKAGE_RISCV_FIRMWARE_CORE_E902),y)
RISCV_FIRMWARE_CORE = e902
else
RISCV_FIRMWARE_CORE = e906
endif

define RISCV_FIRMWARE_BUILD_CMDS
	if [ -f $(@D)/riscv-firmware/apps/Makefile ]; then \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/riscv-firmware/apps clean && \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/riscv-firmware/apps all CORE=$(RISCV_FIRMWARE_CORE) HOST_CXX="$(TARGET_CXX)"; \
	fi
	$(TARGET_CC) $(TARGET_CFLAGS) $(@D)/riscv-firmware/tools/riscv-load.c -o $(@D)/riscv-firmware/tools/riscv-load
endef

# The default app installed as riscv-firmware.elf (and e902-firmware.elf on the A733).
ifeq ($(BR2_PACKAGE_RISCV_FIRMWARE_APP_EXAMPLERISCV),y)
RISCV_FIRMWARE_DEFAULT_APP = exampleRiscv
else ifeq ($(BR2_PACKAGE_RISCV_FIRMWARE_APP_TESTPINGRPMSG),y)
RISCV_FIRMWARE_DEFAULT_APP = testPingRpmsg
else ifeq ($(BR2_PACKAGE_RISCV_FIRMWARE_APP_TESTMSGBOX),y)
RISCV_FIRMWARE_DEFAULT_APP = testMsgbox
else
RISCV_FIRMWARE_DEFAULT_APP = testStringBinaryTrace0
endif

# Install only from bin/$(CORE). bin/ itself is refreshed by E906 builds only, so on
# the A733 its ELFs are stale E906 builds (RV32I, linked at 0x3ffc0000).
RISCV_FIRMWARE_BIN = $(@D)/riscv-firmware/bin/$(RISCV_FIRMWARE_CORE)

define RISCV_FIRMWARE_INSTALL_TARGET_CMDS
	test -f $(RISCV_FIRMWARE_BIN)/$(RISCV_FIRMWARE_DEFAULT_APP).elf || \
		{ echo "riscv-firmware: $(RISCV_FIRMWARE_BIN)/$(RISCV_FIRMWARE_DEFAULT_APP).elf missing" >&2; exit 1; }
	mkdir -p $(TARGET_DIR)/lib/firmware $(TARGET_DIR)/usr/bin $(TARGET_DIR)/usr/share/riscv-firmware

	# All firmware ELFs and binaries built for the active core
	for f in $(RISCV_FIRMWARE_BIN)/*.elf $(RISCV_FIRMWARE_BIN)/*.bin; do \
		[ -f "$$f" ] && $(INSTALL) -D -m 0644 "$$f" $(TARGET_DIR)/lib/firmware/$$(basename "$$f"); \
	done

	# Default firmware: riscv-firmware.elf for /etc/init.d/S60riscv, and the A733
	# remoteproc firmware-name (cubie-a7a-rproc.dtso)
	ln -sf $(RISCV_FIRMWARE_DEFAULT_APP).elf $(TARGET_DIR)/lib/firmware/riscv-firmware.elf
	$(INSTALL) -D -m 0755 $(RISCV_FIRMWARE_BIN)/$(RISCV_FIRMWARE_DEFAULT_APP).elf \
		$(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf
	if [ "$(RISCV_FIRMWARE_CORE)" = "e902" ]; then \
		ln -sf $(RISCV_FIRMWARE_DEFAULT_APP).elf $(TARGET_DIR)/lib/firmware/e902-firmware.elf; \
	fi

	if [ "$(BR2_PACKAGE_RISCV_FIRMWARE_HOST_TOOLS)" = "y" ]; then \
		for tool in ping_shm ping_uio ping_rpmsg stress_rpmsg ping_dram monitor_trace.py fast_sram_telemetry.py; do \
			if [ -f $(RISCV_FIRMWARE_BIN)/$$tool ]; then \
				$(INSTALL) -D -m 0755 $(RISCV_FIRMWARE_BIN)/$$tool $(TARGET_DIR)/usr/bin/$$tool; \
			fi; \
		done; \
		$(INSTALL) -D -m 0755 $(@D)/riscv-firmware/tools/riscv-load $(TARGET_DIR)/usr/bin/riscv-load; \
		$(INSTALL) -D -m 0755 $(@D)/riscv-firmware/tools/load-riscv.sh $(TARGET_DIR)/usr/bin/load-riscv.sh; \
		$(INSTALL) -D -m 0755 $(@D)/riscv-firmware/tools/test_riscv.py $(TARGET_DIR)/usr/bin/test_riscv.py; \
		$(INSTALL) -D -m 0755 $(@D)/riscv-firmware/tools/test_dual_msgbox.py $(TARGET_DIR)/usr/bin/test_dual_msgbox.py; \
	fi
endef

RISCV_FIRMWARE_INSTALL_IMAGES = YES

define RISCV_FIRMWARE_INSTALL_IMAGES_CMDS
	for elf in $(RISCV_FIRMWARE_BIN)/*.elf; do \
		[ -f "$$elf" ] && $(INSTALL) -D -m 0644 "$$elf" $(BINARIES_DIR)/$$(basename "$$elf"); \
	done
	$(INSTALL) -D -m 0644 $(RISCV_FIRMWARE_BIN)/$(RISCV_FIRMWARE_DEFAULT_APP).elf $(BINARIES_DIR)/riscv-firmware.elf
endef

$(eval $(generic-package))
