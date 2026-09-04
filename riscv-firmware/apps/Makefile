# Top-level Apps Makefile for XuanTie E907 (Allwinner T527)

APPS = testBasic testBasicTrace0 testStringBinaryTrace0 testCrash testPing testPingRpmsg testDRAMMsg exampleRiscv
HOST_APPS = testPing/linux testPingRpmsg/linux testDRAMMsg/linux
BIN_DIR = ../bin

all: $(APPS) host-tools stage-bin

$(APPS):
	@echo "================================================================"
	@echo " Building RISC-V App: $@ "
	@echo "================================================================"
	$(MAKE) -C $@

host-tools: $(HOST_APPS)

$(HOST_APPS):
	@echo "================================================================"
	@echo " Building Host Linux App: $@ "
	@echo "================================================================"
	$(MAKE) -C $@

stage-bin: $(APPS) host-tools
	@mkdir -p $(BIN_DIR)
	@for app in $(APPS); do \
		if [ -f $$app/$$app.elf ]; then \
			cp -v $$app/$$app.elf $(BIN_DIR)/; \
			cp -v $$app/$$app.bin $(BIN_DIR)/; \
		fi \
	done
	@if [ -f testPing/linux/ping_shm ]; then \
		cp -v testPing/linux/ping_shm $(BIN_DIR)/; \
	fi
	@if [ -f testPingRpmsg/linux/ping_rpmsg ]; then \
		cp -v testPingRpmsg/linux/ping_rpmsg $(BIN_DIR)/; \
	fi
	@if [ -f testDRAMMsg/linux/ping_dram ]; then \
		cp -v testDRAMMsg/linux/ping_dram $(BIN_DIR)/; \
	fi
	@echo "================================================================"
	@echo " All RISC-V Firmware ELFs staged in $(BIN_DIR):"
	@ls -la $(BIN_DIR)/*.elf
	@echo "================================================================"


clean:
	@for app in $(APPS); do \
		echo "Cleaning $$app..."; \
		$(MAKE) -C $$app clean; \
	done
	@for host_app in $(HOST_APPS); do \
		echo "Cleaning $$host_app..."; \
		$(MAKE) -C $$host_app clean; \
	done
	rm -rf $(BIN_DIR)

.PHONY: all clean $(APPS) host-tools $(HOST_APPS) stage-bin


