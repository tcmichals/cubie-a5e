# Top-level Apps Makefile for XuanTie RISC-V Co-Processors:
#   E906 (Allwinner T527 / A527, RV32IMAFCX)
#   E902 (Allwinner A733, RV32EMC)

CORE ?= e906

ifeq ($(CORE),e902)
  APPS = testFreeRTOS testBasic testCrash testPing testPingRpmsg testPingRpmsgSram testDRAMMsg testMsgbox testMsgboxStep testStringBinaryTrace0 exampleRiscv
else
  APPS = testFreeRTOS testBasic testExtraRam testStringBinaryTrace0 testCrash testPing testPingRpmsg testPingRpmsgSram testDRAMMsg testMsgbox testMsgboxStep exampleRiscv
endif

HOST_APPS = testPing/linux testPingRpmsg/linux testDRAMMsg/linux
BIN_DIR = ../bin/$(CORE)
ROOT_BIN_DIR = ../bin

all: $(APPS) host-tools stage-bin

e906:
	$(MAKE) CORE=e906 all

e902:
	$(MAKE) CORE=e902 all

all-cores:
	$(MAKE) CORE=e906 all
	$(MAKE) CORE=e902 all

$(APPS):
	@echo "================================================================"
	@echo " Building RISC-V App for [$(CORE)]: $@ "
	@echo "================================================================"
	$(MAKE) -C $@ CORE=$(CORE)

host-tools: $(HOST_APPS)

$(HOST_APPS):
	@echo "================================================================"
	@echo " Building Host Linux App: $@ "
	@echo "================================================================"
	$(MAKE) -C $@ $(if $(HOST_CXX),CXX="$(HOST_CXX)")

stage-bin: $(APPS) host-tools
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(ROOT_BIN_DIR)
	@for app in $(APPS); do \
		if [ -f $$app/$$app.elf ]; then \
			cp -v $$app/$$app.elf $(BIN_DIR)/; \
			cp -v $$app/$$app.bin $(BIN_DIR)/; \
			if [ -f $$app/$$app\_devmem.md ]; then cp -v $$app/$$app\_devmem.md $(BIN_DIR)/; fi; \
			if [ "$(CORE)" = "e906" ]; then \
				cp -v $$app/$$app.elf $(ROOT_BIN_DIR)/; \
				cp -v $$app/$$app.bin $(ROOT_BIN_DIR)/; \
				if [ -f $$app/$$app\_devmem.md ]; then cp -v $$app/$$app\_devmem.md $(ROOT_BIN_DIR)/; fi; \
			fi; \
		fi \
	done
	@for mem_tool in e906_mem.py riscv_mem.py; do \
		if [ -f ../tools/$$mem_tool ]; then \
			cp -v ../tools/$$mem_tool $(BIN_DIR)/; \
			cp -v ../tools/$$mem_tool $(ROOT_BIN_DIR)/; \
			chmod +x $(BIN_DIR)/$$mem_tool $(ROOT_BIN_DIR)/$$mem_tool; \
		fi; \
	done
	@if [ -f testPing/linux/ping_shm ]; then \
		cp -v testPing/linux/ping_shm $(BIN_DIR)/; \
		cp -v testPing/linux/ping_shm $(ROOT_BIN_DIR)/; \
	fi
	@if [ -f testPing/linux/ping_uio ]; then \
		cp -v testPing/linux/ping_uio $(BIN_DIR)/; \
		cp -v testPing/linux/ping_uio $(ROOT_BIN_DIR)/; \
	fi
	@if [ -f testPing/linux/ping_uio.py ]; then \
		cp -v testPing/linux/ping_uio.py $(BIN_DIR)/; \
		cp -v testPing/linux/ping_uio.py $(ROOT_BIN_DIR)/; \
		chmod +x $(BIN_DIR)/ping_uio.py $(ROOT_BIN_DIR)/ping_uio.py; \
	fi
	@if [ -f testPingRpmsg/linux/ping_rpmsg ]; then \
		cp -v testPingRpmsg/linux/ping_rpmsg $(BIN_DIR)/; \
		cp -v testPingRpmsg/linux/ping_rpmsg $(ROOT_BIN_DIR)/; \
	fi
	@if [ -f testPingRpmsg/linux/stress_rpmsg ]; then \
		cp -v testPingRpmsg/linux/stress_rpmsg $(BIN_DIR)/; \
		cp -v testPingRpmsg/linux/stress_rpmsg $(ROOT_BIN_DIR)/; \
	fi
	@if [ -f testPingRpmsg/linux/ping_rpmsg.py ]; then \
		cp -v testPingRpmsg/linux/ping_rpmsg.py $(BIN_DIR)/; \
		cp -v testPingRpmsg/linux/ping_rpmsg.py $(ROOT_BIN_DIR)/; \
		chmod +x $(BIN_DIR)/ping_rpmsg.py $(ROOT_BIN_DIR)/ping_rpmsg.py; \
	fi
	@if [ -f testDRAMMsg/linux/ping_dram ]; then \
		cp -v testDRAMMsg/linux/ping_dram $(BIN_DIR)/; \
		cp -v testDRAMMsg/linux/ping_dram $(ROOT_BIN_DIR)/; \
	fi
	@if [ -f testStringBinaryTrace0/monitor_trace.py ]; then \
		cp -v testStringBinaryTrace0/monitor_trace.py $(BIN_DIR)/; \
		cp -v testStringBinaryTrace0/monitor_trace.py $(ROOT_BIN_DIR)/; \
		chmod +x $(BIN_DIR)/monitor_trace.py $(ROOT_BIN_DIR)/monitor_trace.py; \
	fi
	@if [ -f testStringBinaryTrace0/fast_sram_telemetry.py ]; then \
		cp -v testStringBinaryTrace0/fast_sram_telemetry.py $(BIN_DIR)/; \
		cp -v testStringBinaryTrace0/fast_sram_telemetry.py $(ROOT_BIN_DIR)/; \
		chmod +x $(BIN_DIR)/fast_sram_telemetry.py $(ROOT_BIN_DIR)/fast_sram_telemetry.py; \
	fi
	@if [ -f ../tools/run_tests.py ]; then \
		cp -v ../tools/run_tests.py $(BIN_DIR)/; \
		cp -v ../tools/run_tests.py $(ROOT_BIN_DIR)/; \
		chmod +x $(BIN_DIR)/run_tests.py $(ROOT_BIN_DIR)/run_tests.py; \
	fi
	@echo "================================================================"
	@echo " All RISC-V [$(CORE)] Firmware ELFs staged in $(BIN_DIR):"
	@ls -la $(BIN_DIR)/*.elf
	@echo "================================================================"


clean:
	@for app in testFreeRTOS testBasic testExtraRam testStringBinaryTrace0 testCrash testPing testPingRpmsg testPingRpmsgSram testDRAMMsg testMsgbox testMsgboxStep exampleRiscv; do \
		echo "Cleaning $$app..."; \
		$(MAKE) -C $$app clean; \
	done
	@for host_app in $(HOST_APPS); do \
		echo "Cleaning $$host_app..."; \
		$(MAKE) -C $$host_app clean; \
	done
	rm -rf $(ROOT_BIN_DIR)/e906 $(ROOT_BIN_DIR)/e902

.PHONY: all clean $(APPS) host-tools $(HOST_APPS) stage-bin


