# Cubie Project Roadmap & Multi-Board Dashboard

This repository supports software and hardware bring-up for two distinct Allwinner-based single board computers:
1. **Radxa Cubie A5E** (Allwinner A527 / T527)
2. **Radxa Cubie A7A** (Allwinner A733)

To ensure clarity, each board has a dedicated, comprehensive tracking document. Use the links below to navigate to board-specific milestones and tasks.

---

## Board Navigation & Status Matrix

| Board Platform | SoC / Architecture | Co-Processor | Current Status | Primary Tracking Document |
| :--- | :--- | :--- | :--- | :--- |
| **Radxa Cubie A5E** | Allwinner A527 / T527<br>(8× Cortex-A55 @ 1.8 GHz) | XuanTie E907<br>(200 MHz, RV32IMAFDC) | **Production Bring-Up**<br>• RemoteProc & Mailbox Upstream RFC Ready<br>• Sub-15 $\mu$s IPC Verified<br>• Next: Camera Encoding (VPU) & NPU | 📄 [**TODO_A5E.md**](TODO_A5E.md) |
| **Radxa Cubie A7A** | Allwinner A733<br>(4× A76 + 4× A55) | XuanTie E902<br>(200 MHz, RV32EMC) | **Active Silicon Bring-Up**<br>• Active Focus: USB & Power (FE1.1S / AIC8800)<br>• Active Blocker: Ethernet GMAC210 TX DMA<br>• Architecture: Dual-Mode E902 RemoteProc | 📄 [**TODO_A7A.md**](TODO_A7A.md) |

---

## Radxa Cubie A5E Executive Summary

* **Dedicated Roadmap**: [**`TODO_A5E.md`**](TODO_A5E.md)
* **Status**: Highly mature; upstream Linux submission ready.
* **Top Active Priorities**:
  1. **Camera & Hardware Video Encoding (VPU / Cedrus)**:
     - MIPI-CSI / parallel camera capture (`/dev/video0`).
     - Enable `CONFIG_VIDEO_SUNXI_CEDRUS=y` with stateless `v4l2_m2m` H.264/H.265 encoding.
     - Zero-copy `dma-buf` pipeline passing camera frames directly into Cedrus hardware encoder without CPU `memcpy`.
  2. **NPU Deep Learning Acceleration (2 TOPS TinyML)**:
     - Enable mainline `CONFIG_DRM_ETNAVIV=y` for onboard Vivante VIP9000 (`npu@7122000`).
     - Build Mesa with Etnaviv Gallium and Teflon delegate (`libteflon.so`).
     - Validate TensorFlow Lite INT8 quantized models on target silicon.
  3. **XuanTie E907 Advanced RemoteProc & IPC**:
     - Complete 4-tier benchmark matrix with pure on-chip SRAM VirtIO (`testPingRpmsgSram`).
     - **Automatic Core Restart After Crash**: Wire E907 trap handler alert to `rproc_report_crash()` in `sunxi_rproc.c` for automatic firmware reload and restart without host reboot.
     - **System Suspend / Resume**: Implement `dev_pm_ops` in `sunxi_rproc.c` and validate sleep retention in SRAM (`echo mem > /sys/power/state`).
     - Multi-channel concurrency & high-load stress testing (10+ worker threads).
     - Cadence Tensilica HiFi4 DSP co-processor bring-up.
  4. **Upstream Linux Submission**:
     - 5-patch series in `patches-upstream-rfc/` passing `checkpatch.pl` 100% clean (0 errors).
     - Ready for submission to `linux-sunxi@lists.linux.dev` and `linux-remoteproc@vger.kernel.org`.

---

## Radxa Cubie A7A Executive Summary

* **Dedicated Roadmap**: [**`TODO_A7A.md`**](TODO_A7A.md)
* **Status**: Board bring-up in progress.
* **Top Active Priorities**:
  1. **USB & Power Subsystem (Active Focus)**:
     - Validate FE1.1S USB 2.0 4-port hub enumeration on Host 1 (`ehci1`).
     - Validate AIC8800 Wi-Fi 6 / BT 5.4 module enumeration on hub port 4 (`0xA69C:0x8800`).
     - Verify power rails and GPIO regulators (`PL2`, `PM5`, `PM0`, `PM1`).
  2. **Ethernet GMAC210 TX DMA Watchdog (Active Blocker)**:
     - Resolve recurring `NETDEV WATCHDOG: transmit queue 0 timed out`.
     - Implement multi-MSI queue IRQ handling (`STMMAC_FLAG_MULTI_MSI_EN`, SPI 173/174) in A733 stmmac glue.
  3. **XuanTie E902 Co-Processor RemoteProc (Mode 2 Bring-Up)**:
     - Post-USB bring-up: Unlock BL31 `R_SPC` (`0x07002000`) and `R_TZMA` (`0x07003000`) for Non-Secure Linux access.
     - Map System SRAM A2 (`0x00040000`, 208 KB) in `sunxi_rproc.c`.

---

## Supporting Engineering References

* **A5E RemoteProc & Benchmarks**: `riscv-firmware/TODO.md`, `riscv-firmware/tests.md`
* **Upstream RFC Patch Series**: `patches-upstream-rfc/`
* **Upstream Validation Script**: `tools/check_upstream_rfc.sh`
* **A7A Bring-Up Debug Log**: `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`
* **A7A Ethernet Reference**: `docs/platforms/CUBIE_A7A_ETHERNET_SCHEMATIC_REFERENCE.md`
* **A7A Coprocessor Architecture**: `docs/A733_E902_BOOT_AND_COPROCESSOR_ARCHITECTURE.md`
