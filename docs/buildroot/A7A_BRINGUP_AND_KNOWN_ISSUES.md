# 🚀 Radxa Cubie A7A (Allwinner A733 / `sun60iw2`) Bring-Up & Known Issues

This document provides a comprehensive technical reference for the **Radxa Cubie A7A** single-board computer powered by the **Allwinner A733 (`sun60iw2`)** SoC running **Linux 7.1 PREEMPT_RT**. It details the hardware architecture, upstream mainline status, every bring-up issue encountered, root cause analyses, and verified fixes for community sharing and upstream submission.

---

## 1. Hardware & System Architecture

| Subsystem | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A733 (`sun60iw2p1`) | 2× Cortex-A76 (Big) + 6× Cortex-A55 (LITTLE) DynamIQ Cluster |
| **Co-Processor** | XuanTie E907 RISC-V (32-bit RV32IMAFCP) | Deterministic real-time I/O & flight control coprocessor |
| **RAM** | 6 GiB LPDDR5 (400 MHz to 2400 MHz) | Multi-PState dynamic hardware calibration via `boot0` |
| **Interrupts** | ARM GICv3 (GIC-600) | Distributor: `0x03400000`, Redistributors: `0x03460000` |
| **Boot Chain** | `BROM` $\rightarrow$ `boot0` (SRAM) $\rightarrow$ `BL31` $\rightarrow$ `U-Boot 2018.07` | Packaged as 16 MB binary blob `radxa_a733_bootloader.bin` |
| **Mainline Status** | **Active Upstream Review** (Patches in `linux-sunxi`/`linux-clk`) | Integrating `ccu-sun60i-a733` & `pinctrl-sun60i-a733` series |
| **Wi-Fi / BT** | AIC8800D80 via **USB 2.0 High-Speed** | `0xA69C:0x8800` (Unlike Cubie A5E which uses SDIO) |

---

## 2. Issues Encountered, Root Causes & Verified Fixes

### Issue 1: GICv2 vs GICv3 Firmware Panic
- **Symptom**:
  ```text
  [ 0.000000] Root IRQ handler: gic_handle_irq
  [ 0.000000] GIC CPU mask not found - kernel will fail to boot.
  [ 0.000000] GICv3 system registers enabled, broken firmware!
  [ 0.000000] WARNING: drivers/irqchip/irq-gic.c:57 at gic_cpu_init+0x100/0x108
  ```
- **Root Cause**: The legacy vendor device tree declared GICv2 (`compatible = "arm,cortex-a15-gic"` at `0x03021000`), but Allwinner ARM Trusted Firmware (BL31) configured the CPU interfaces with GICv3 System Register Enable (`ICC_SRE_EL1.SRE = 1`). Mainline Linux 7.1 strictly rejects GICv2 MMIO access when GICv3 system registers are active.
- **Fix**: Replaced the interrupt-controller node in `sun60i-a733-cubie-a7a.dts` with native GICv3 mapping:
  ```dts
  interrupt-controller@3400000 {
      compatible = "arm,gic-v3";
      #interrupt-cells = <0x03>;
      #address-cells = <0x02>;
      #size-cells = <0x02>;
      ranges;
      interrupt-controller;
      reg = <0x00 0x03400000 0x00 0x10000>,
            <0x00 0x03460000 0x00 0x100000>;
      interrupts = <0x01 0x09 0x04>;
      interrupt-parent = <0x9b>;
      dma-noncoherent;
      phandle = <0x9b>;

      its: msi-controller@3440000 {
          compatible = "arm,gic-v3-its";
          reg = <0x00 0x03440000 0x00 0x20000>;
          msi-controller;
          #msi-cells = <0x01>;
          dma-noncoherent;
      };
  };
  ```

---

### Issue 2: Silent Hang at BL3-1 Entry (The `sun55i-a523.dtsi` Inclusion Trap)
- **Symptom**:
  ```text
  NOTICE:  BL3-1: Next image address = 0x40200000
  NOTICE:  BL3-1: Next image spsr = 0x3c5
  <hang - no earlycon output>
  ```
- **Root Cause**: Attempting to clean up the A733 DTS by including `sun55i-a523.dtsi` (the A523 SoC tree) caused silent failure. The Allwinner A733 (`sun60iw2`) has an entirely distinct clock control unit (CCU), power domain map, and CPU topology (2× A76 + 6× A55 DynamIQ vs 8× A55). The A523 CCU starved the kernel UART and CPUs of required clocks.
- **Fix**: Retain the complete 2,564-line native `sun60i` hardware device tree and apply fixes surgically without including `sun55i-a523.dtsi`.

---

### Issue 3: Vendor U-Boot 2018.07 DRAM Scan Failure
- **Symptom**:
  ```text
  [15.485]## error: update_fdt_dram_para_from_bootpara : FDT_ERR_NOTFOUND
  ```
- **Root Cause**: In modern mainline Linux and U-Boot, `/memory` nodes are omitted from board DTS files because modern U-Boot injects the node dynamically. However, Allwinner's vendor U-Boot 2018.07 calls `fdt_path_offset(fdt, "/memory")` to look up an *existing* node. When absent, vendor U-Boot fails to pass the physical memory map to Linux, leaving the kernel with 0 MB RAM.
- **Fix**: Explicitly define the base 6 GiB memory node in `sun60i-a733-cubie-a7a.dts`:
  ```dts
  memory@40000000 {
      device_type = "memory";
      reg = <0x00 0x40000000 0x01 0x80000000>; /* 6 GiB: 0x40000000 - 0x1C0000000 */
  };
  ```

---

### Issue 4: Stale DTB Overwrite in `post-image.sh`
- **Symptom**: DTB modifications compiled during `make linux-rebuild` (30,886 bytes) were mysteriously reverting to 42,471 bytes in the final `sdcard.img`.
- **Root Cause**: `board/radxa/cubie_a7a/post-image.sh` contained a legacy copy line:
  ```bash
  cp -f "${BOARD_DIR}/sun60i-a733-cubie-a7a.dtb" "${BINARIES_DIR}/sun60i-a733-cubie-a7a.dtb"
  ```
  which silently overwrote the kernel's compiled DTB with a stale binary on every `make` invocation.
- **Fix**: Removed the `cp -f` line in `post-image.sh` so the build system always packages the freshly compiled kernel DTB.

---

### Issue 5: XuanTie E907 RISC-V Co-Processor Lifecycle Management
- **Symptom**: Legacy userspace register and memory pokers (`riscv-load`) caused memory corruption and failed under strict physical memory protections (`CONFIG_STRICT_DEVMEM`).
- **Fix**: Ported a clean kernel-level `sunxi_rproc.c` remoteproc driver ([`project-cubie-a5e/patches/linux/0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch`](/project-cubie-a5e/patches/linux/0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch)) supporting:
  - Automatic ELF parsing into PubSRAM C (`0x00020000`) and Dedicated MCU SRAM (`0x3FFC0000`).
  - Kernel CCF clock gating and reset assertions.
  - Standard `/sys/class/remoteproc/remoteproc0/state` lifecycle control.
  - Purged `iomem=relaxed` from bootargs, restoring hardware memory safety.

---

### Issue 6: AIC8800 Wi-Fi Bus Mismatch (USB on A7A vs SDIO on A5E)
- **Symptom**: Upstream `wireless-next` RFC v2 patch for AIC8800 was SDIO-only. On the Cubie A7A, the AIC8800 is wired to USB (`0xA69C:0x8800`), causing probe failures.
- **Fix**: Unified the driver codebase into [`aic8800-upstream/`](/aic8800-upstream/):
  - Added clean USB transport (`aicwf_usb.c`, `usb_host.c`) with asynchronous URBs.
  - Built `bld.a7a` with `CONFIG_USB_SUPPORT=y` (generating standalone `aic8800_fdrv.ko`).
  - Built `bld.a5e` with `CONFIG_SDIO_SUPPORT=y` (generating `aic8800_bsp.ko` + `aic8800_fdrv.ko`).

---

### Issue 7: Ethernet (GMAC0) PHY Reset GPIO & RX Pinmux Conflict
- **Symptom**: Ethernet link down or receive packet framing errors; Gigabit PHY reset unresponsive.
- **Root Cause**: Datasheet V0.93 Table 4-37 & Schematic Sheet 7 prove `PH10` is ball `1AC2` (`RGMII0-RXD3`), whereas the Motorcomm PHY reset is routed to **`PH16`** (`GMAC1_RSTn_L` via `R185` 0Ω). The early mainline DTS drove `PH10` as a GPIO reset output (forcing RX bit 3 to 0) and omitted `PH10` from `gmac0_pins`.
- **Fix**: Added `"PH10"` back into `gmac0_pins` (all 16 pins `PH0`–`PH15`) and updated `reset-gpios = <&pio 7 16 GPIO_ACTIVE_LOW>;` (`PH16`).

---

### Issue 8: Fatal Mailbox (MSGBOX0) CCU Null Pointer & CPU PLL Corruption
- **Symptom**: Hard SoC freeze / kernel hang during early device driver probing.
- **Root Cause**: `mailbox@3004000` requested `resets = <&ccu RST_BUS_MSGBOX0>`, but `CLK_MSGBOX0` and `RST_BUS_MSGBOX0` were omitted from `ccu-sun60i-a733.c`. Reset index 7 remained uninitialized `{0x0000, 0}`. Calling `reset_control_deassert()` performed a 32-bit write to CCU offset `0x0000` (`CLK_PLL_CPUX`), corrupting CPU core frequencies.
- **Fix**: Registered `bus_msgbox0_clk` at `0x0744 BIT(0)` in `sun60i_a733_hw_clks[CLK_MSGBOX0]` and `[RST_BUS_MSGBOX0] = { 0x0744, BIT(16) }` in `sun60i_a733_ccu_resets[]`.

---

### Issue 9: USB 0 / USB 1 EHCI Host DMA Engines Gated in Silicon
- **Symptom**: Onboard FE1.1S USB 2.0 4-port hub and AIC8800 Wi-Fi 6 module failed to appear in `lsusb`.
- **Root Cause**: In `ccu-sun60i-a733.c`, `bus_usb0_clk` only enabled `BIT(0)` (OHCI) and `BIT(16)` (OHCI reset). In vendor silicon (`ccu-sun60iw2.c`), EHCI gate is `BIT(4)` and EHCI reset is `BIT(20)`.
- **Fix**: Updated `bus_usb0_clk`/`bus_usb1_clk` to `BIT(4)|BIT(0)` and `RST_BUS_USB0`/`RST_BUS_USB1` to `BIT(20)|BIT(16)` at `0x1304`/`0x130c`.

---

### Issue 10: PRCM R-CCU `CLK_R_AHB` Register Offset Shift
- **Symptom**: PRCM bus hangs when probing `r_pio: pinctrl@7025000` and PMIC communication.
- **Root Cause**: `ccu-sun60i-a733-r.c` defined `r_ahb_clk` at register offset `0x004` (non-existent). Real silicon (`ccu-sun60iw2-r.c`) has `r-ahb` at offset `0x0000`.
- **Fix**: Realigned `r_ahb_clk` to register offset `0x000` in `ccu-sun60i-a733-r.c`.

---

## 3. Upstream Patch Tracking & Mainline Integration Roadmap

The fundamental blockers to running vanilla mainline on the A733 are currently being reviewed in the Linux kernel and U-Boot communities:

1. **Clock Controller (`drivers/clk/sunxi-ng/ccu-sun60i-a733.c`)**: Authored by Junhui Liu, introduces clock gates, PLL dividers, and reset controls.
2. **RTC & Base Clocks (`drivers/clk/sunxi-ng/sun6i-rtc.c`)**: Authored by Jerome Brunet & Chen-Yu Tsai, queued for Linux 7.3.
3. **Pin Controller (`drivers/pinctrl/sunxi/pinctrl-sun60i-a733.c`)**: Authored by Yixun Lan, provides PIO and R-PIO pin muxing.
4. **Automated Tracking Tool (`tools/watch_a733_upstream.py`)**: Live CLI monitoring tool that queries `lore.kernel.org` and U-Boot Patchwork to notify developers when newer patch revisions (v3, v4, etc.) are available.

---

## 4. Verified Artifacts & Verification Checklist

| Artifact | Size | Target Location | Verification |
| :--- | :--- | :--- | :--- |
| **`sun60i-a733-cubie-a7a.dtb`** | `41,583 bytes` | `bld.a7a/images/sun60i-a733-cubie-a7a.dtb` | Contains GICv3 (`0x03400000`) & 6 GiB RAM |
| **`Image` (Linux 7.1 PREEMPT_RT)** | `44,395,008 bytes` | `bld.a7a/images/Image` | Built-in `sunxi_rproc.o` |
| **`sdcard.img`** | `620,756,992 bytes` | `bld.a7a/images/sdcard.img` | Ready to flash |

