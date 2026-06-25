# timvx-delegate integration package

TIMVX_DELEGATE_PREBUILT_DIR = $(call qstrip,$(BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR))

define TIMVX_DELEGATE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 \
		$(BR2_EXTERNAL_CUBIE_A5E_PATH)/board/radxa/cubie_a5e/rootfs-overlay/usr/bin/npu-smoke-test \
		$(TARGET_DIR)/usr/bin/npu-smoke-test
	@if [ -n "$(TIMVX_DELEGATE_PREBUILT_DIR)" ] && [ -d "$(TIMVX_DELEGATE_PREBUILT_DIR)" ]; then \
		echo "timvx-delegate: installing runtime from $(TIMVX_DELEGATE_PREBUILT_DIR)"; \
		$(INSTALL) -d $(TARGET_DIR)/usr/lib $(TARGET_DIR)/usr/bin; \
		if [ -d "$(TIMVX_DELEGATE_PREBUILT_DIR)/lib" ]; then \
			find "$(TIMVX_DELEGATE_PREBUILT_DIR)/lib" -maxdepth 1 -type f \
				\( -name "*.so" -o -name "*.so.*" \) -exec cp -a {} $(TARGET_DIR)/usr/lib/ \;; \
		fi; \
		if [ -d "$(TIMVX_DELEGATE_PREBUILT_DIR)/bin" ]; then \
			find "$(TIMVX_DELEGATE_PREBUILT_DIR)/bin" -maxdepth 1 -type f -executable \
				-exec cp -a {} $(TARGET_DIR)/usr/bin/ \;; \
		fi; \
	else \
		echo "timvx-delegate: BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR is not set or missing; runtime copy skipped"; \
		echo "timvx-delegate: npu-smoke-test will report missing delegate libs until runtime bundle is provided"; \
	fi
endef

$(eval $(generic-package))
