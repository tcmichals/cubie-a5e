# ==============================================================================
# Cadence Tensilica HiFi4 Audio DSP Firmware Top-Level Makefile
# Allwinner T527 / A527 (Radxa Cubie A5E)
# ==============================================================================

APPS = testBasic testMsgbox

all: $(APPS)

$(APPS):
	@$(MAKE) -C apps/$@ all

clean:
	@for app in $(APPS); do \
		$(MAKE) -C apps/$$app clean; \
	done

.PHONY: all clean $(APPS)
