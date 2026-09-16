################################################################################
#
# dsp-hifi4
#
################################################################################

DSP_HIFI4_VERSION = 1.0
DSP_HIFI4_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../firmware
DSP_HIFI4_SITE_METHOD = local

define DSP_HIFI4_BUILD_CMDS
	$(MAKE) -C $(@D)/hifi4-dsp clean && \
	$(MAKE) -C $(@D)/hifi4-dsp all
endef

define DSP_HIFI4_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/lib/firmware
	mkdir -p $(TARGET_DIR)/usr/share/dsp-hifi4

	if [ -d $(@D)/hifi4-dsp/apps/testBasic/build ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.elf $(TARGET_DIR)/lib/firmware/dsp-testBasic.elf; \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.bin $(TARGET_DIR)/lib/firmware/dsp-testBasic.bin; \
	fi
	if [ -d $(@D)/hifi4-dsp/apps/testMsgbox/build ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.elf $(TARGET_DIR)/lib/firmware/dsp-testMsgbox.elf; \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.bin $(TARGET_DIR)/lib/firmware/dsp-testMsgbox.bin; \
	fi

	# Default active DSP firmware loaded by Linux remoteproc
	if [ "$(BR2_PACKAGE_DSP_HIFI4_APP_TESTBASIC)" = "y" ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.elf $(TARGET_DIR)/lib/firmware/dsp-firmware.elf; \
		$(INSTALL) -D -m 0755 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.elf $(TARGET_DIR)/usr/share/dsp-hifi4/firmware.elf; \
	else \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.elf $(TARGET_DIR)/lib/firmware/dsp-firmware.elf; \
		$(INSTALL) -D -m 0755 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.elf $(TARGET_DIR)/usr/share/dsp-hifi4/firmware.elf; \
	fi
endef

DSP_HIFI4_INSTALL_IMAGES = YES

define DSP_HIFI4_INSTALL_IMAGES_CMDS
	if [ -d $(@D)/hifi4-dsp/apps/testBasic/build ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.elf $(BINARIES_DIR)/dsp-testBasic.elf; \
	fi
	if [ -d $(@D)/hifi4-dsp/apps/testMsgbox/build ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.elf $(BINARIES_DIR)/dsp-testMsgbox.elf; \
	fi

	if [ "$(BR2_PACKAGE_DSP_HIFI4_APP_TESTBASIC)" = "y" ]; then \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testBasic/build/testBasic.elf $(BINARIES_DIR)/dsp-firmware.elf; \
	else \
		$(INSTALL) -D -m 0644 $(@D)/hifi4-dsp/apps/testMsgbox/build/testMsgbox.elf $(BINARIES_DIR)/dsp-firmware.elf; \
	fi
endef

$(eval $(generic-package))
