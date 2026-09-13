# Cubie A7A Bring-Up TODO

> **Restart here.** Last updated: 2026-09-01.
>
> Active scope: Ethernet and E907 remoteproc in parallel. USB remains disabled. Do not change serial transport for these tasks.

## Mandatory patch gate

- [x] Clean Buildroot Linux validation passed: `make -C bld.a7a linux-dirclean && make -C bld.a7a linux` reapplied all A7A external kernel patches to clean Linux 7.1 and built the A7A DTB.
- [ ] Before every patch commit, rerun that clean-tree gate.
- [ ] Permanent kernel changes must be synchronized into `project-cubie-a5e/patches/linux/`; never commit a fix that exists only in `bld.a7a/build/linux-7.1/`.

See `docs/buildroot/A7A_KERNEL_PATCH_VALIDATION.md`.

## Ethernet: no physical carrier

- [x] MDIO finds U10 Maxio MAE0621A-Q3C at PHY address 1.
- [x] U-Boot pre-start MDIO reads returned `0x0000` at every address because the Ethernet controller had not entered its normal start path; do not use pre-start `mdio` results as PHY evidence.
- [x] U-Boot target proof: two pings to live peer `192.168.1.2` succeeded, while absent peer `.3` correctly failed. PHY power/reset, MDIO, RGMII, MAC DMA, board routing, magnetics, and cable are functional. The Ethernet fault is isolated to Linux configuration.
- [x] Running U-Boot control DT has GMAC enabled, PH0–PH15 muxed to function 5, PHY address 1, and PH16 active-low reset released high. Treat the old vendor U-Boot source as register/routing evidence only.
- [x] Post-ping U-Boot reads return teardown state (`0x0200341c = 1`, syscfg and standalone MDIO reads zero); they are not active-state references. The successful ping is the valid datapath proof.
- [ ] In Linux, capture `/proc/interrupts` before and after traffic. Vendor GMAC210 explicitly enables split multi-MSI and requests TX0/RX0 IRQs; current `dwmac-sun55i` does not enable multi-MSI, so SPI 173/174 are not requested.
- [ ] If target IRQ counts confirm only inactive `macirq`, review the minimal GMAC210 variant fix: standard queue IRQ names in DTS plus `STMMAC_FLAG_MULTI_MSI_EN` in A733 glue. This cannot be solved in DTS alone.
- [x] A later target boot reports `Link is Up - 1Gbps/Full`; PHY power/reset, copper link, autonegotiation, and basic RGMII MAC/PHY integration are confirmed.
- [x] TX DMA failure confirmed: `NETDEV WATCHDOG: transmit queue 0 timed out` repeats every 5–6 seconds with zero RX and only 1314 TX bytes. Adapter reset/re-probe causes the repeated `Link is Up` messages.
- [x] Demoted MAE0621 probe/version/self-check/remove banners from unconditional `printk()` to `phydev_dbg()`; normal console output now retains only real link transitions and MAC watchdog/reset diagnostics.
- [x] GMAC core-clock parent and `rgmii-id` correction are active: PTP clock now registers and link remains 1 Gbps/full duplex, but TX DMA watchdog timeouts persist.
- [x] Added A733 CCU/DTS resource wiring only: `CLK_GMAC_PTP`, plus distinct GMAC AXI and MAC reset IDs. The A7A DTS now supplies `ptp_ref` and standard `stmmaceth`/`ahb` reset names; MAC and PHY driver code is unchanged.
- [x] Schematic: PHY has its own 25 MHz crystal Y5. The SoC `EPHY-CLK-25M` route is unpopulated (R116), so the CCU output cannot fix PHY link.
- [x] Added `ethtool` and `phytool` to the A7A external defconfig and verified them in the rebuilt target at `/usr/sbin/ethtool` and `/usr/bin/phytool`.
- [x] Built and structurally audited the diagnostic A7A SD image.
- [ ] On target, capture `ethtool eth0`, interface counters, and ping/traffic results while watching `dmesg -w`.
- [ ] If `Link is Up`/`Link is Down` messages recur with different timestamps, capture PHY control/status and autonegotiation registers across the transition to diagnose link flap.
- [ ] Build and target-test the DTS/CCU-only GMAC resource update; verify TX completion before making any other Ethernet change.

See `docs/platforms/CUBIE_A7A_ETHERNET_SCHEMATIC_REFERENCE.md` and `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.

## USB and Power: Verification Phase (Active)

- [x] **Schematic & Power Sequencing**: Decoded V1.10 schematic. Port 0 VBUS (`PL2`), Port 1 / Hub VBUS (`PM5`), Wi-Fi Power (`PM0`), and Wi-Fi Chip Enable (`PM1`) configured as `regulator-always-on` and `regulator-boot-on`.
- [x] **CCU Interconnect & HCI Clocks**: Un-gated `0x05C0` (`AHB_GATE_SW_CFG` bit 9) in CCU probe. Updated `bus_usb0_clk` and `bus_usb1_clk` to mask `BIT(4) | BIT(0)` (`0x1304`/`0x130c`), clocking both EHCI DMA engines and OHCI.
- [x] **PHY SIDDQ & Shared Resets**: Added `sun60i_a733_cfg` in `phy-sun4i-usb.c` clearing `PHY_CTL_SIDDQ | PHY_CTL_H3_SIDDQ` on PMU1, with `devm_reset_control_get_shared()` to prevent `-EBUSY` collisions.
- [ ] **Target Validation**: Boot newly assembled image on Radxa Cubie A7A hardware:
  - Verify EHCI0/1 and OHCI0/1 probe without `-EBUSY`.
  - Verify FE1.1S USB 2.0 hub enumeration on Host 1 (`ehci1`).
  - Verify AIC8800 Wi-Fi 6 device enumeration on hub port 4 (`0xA69C:0x8800`).

## Remoteproc & Real-Time Control: A733 Dual-Mode Architecture

- [x] **Silicon & Security Discovery**: Confirmed A733 coprocessor is **XuanTie E902** (RV32EMC, 200 MHz, no FPU, no TCMs, 208 KB System SRAM A2). Stock BL31 write-protects `0x07032204` for factory `scp.fex`.
- [x] **Dual-Mode Architecture Defined**: Detailed in `docs/A733_E902_BOOT_AND_COPROCESSOR_ARCHITECTURE.md`:
  - **Mode 1 (Suspend/Resume)**: Stock TOC1 with `scp.fex` for consumer S3 deep sleep.
  - **Mode 2 (Real-Time Control / Linux RemoteProc)**: 24/7 industrial/embedded control without suspend/resume.
- [ ] **Post-USB Execution Plan for Mode 2**:
  1. **TF-A (BL31)**: Configure `sunxi_security.c` to unlock `R_SPC` (`0x07002000`) and `R_TZMA` (`0x07003000`) so Non-Secure Linux EL1 can access `0x07032000` and System SRAM A2 (`0x00040000`).
  2. **U-Boot**: Ensure U-Boot RSB driver powers PMIC `DCDC1` and `ALDO1` when `scp.fex` is omitted from TOC1.
  3. **Device Tree**: Add `cubie-a7a-rproc.dtso` overlay defining `&rproc` and `0x4E000000` DMA pool.
  4. **Kernel Driver**: Update `sunxi_rproc.c` with `"allwinner,sun60i-a733-rproc"` to map SRAM A2 (`0x00040000`, 208 KB) and manage E902 lifecycle.

## RemoteProc Driver: Additional Validation & Stress Testing Scope (A5E / T527 / A733)

- [x] **Core Lifecycle & Boot Validation**: Tested `start` -> `stop` -> `start` cycles with zero `/dev/mem` register workarounds.
- [x] **ELF Loader & Address Translation**: Verified SRAM Space 0 (`0x3FFC0000`), SRAM Space 1 (`0x40000000`), and DDR carveout (`0x48000000`).
- [x] **Debugfs Trace Integration**: Verified dynamic `RSC_TRACE` extraction into `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
- [x] **VirtIO RPMsg over DDR DRAM**: Verified `dma_alloc_coherent()` DDR CMA buffers (`0xf2f80000`), mailbox doorbells, PREEMPT_RT deferred workqueue, and 1,000 round-trip 512B packets at 100% success (0 timeouts).
- [x] **Standardized 512-Byte Apples-to-Apples Benchmarks**: Tested `ping_shm` (14.6 us RTT, 61.5 MB/s), `ping_dram` (191.8 us RTT, 4.6 MB/s), and `ping_rpmsg` (175.6 us RTT, 5.37 MB/s).
- [x] **Exception Trapping & Isolation**: Verified `testCrash` captures register autopsy in SRAM while ARM host kernel remains stable.
- [ ] **1. Pure On-Chip SRAM VirtIO RPMsg Benchmark (`testPingRpmsgSram`)**:
  - [ ] Update firmware `.resource_table` with fixed Device Addresses (`.da = 0x40000000` in SRAM Space 1) for Vring 0, Vring 1, and VirtIO payload message buffers instead of dynamic DDR CMA (`FW_RSC_ADDR_ANY`).
  - [ ] Ensure `sunxi_rproc.c` handles SRAM mapping for vrings and buffers without calling `dma_alloc_coherent()`.
  - [ ] Benchmark `ping_rpmsg` with 512-byte buffer length over pure on-chip SRAM.
  - [ ] Complete the 4-way architectural comparison matrix:
    - Pure SRAM Polling (`ping_shm`): **14.59 $\mu\text{s}$**
    - VirtIO in On-Chip SRAM (Projected): **~40 – 50 $\mu\text{s}$**
    - VirtIO in DDR DRAM (`ping_rpmsg`): **175.64 $\mu\text{s}$**
    - Hybrid SRAM/DDR Carveout (`ping_dram`): **191.84 $\mu\text{s}$**
- [ ] **2. Automatic Crash Recovery (`rproc_report_crash`)**:
  - [ ] Implement mailbox/interrupt notification from E907 trap handler to ARM Linux host.
  - [ ] Wire `rproc_report_crash()` in `sunxi_rproc.c` to trigger automatic core recovery/reboot when `recovery = enabled`.
  - [ ] Test automatic recovery cycle on target silicon when `testCrash.elf` fires an illegal instruction.
- [ ] **3. System Power Management (Suspend / Resume / `pm_runtime`)**:
  - [ ] Add runtime PM / system suspend callbacks to `sunxi_rproc.c`.
  - [ ] Test system deep sleep (`echo mem > /sys/power/state`) while XuanTie E907 is running.
  - [ ] Verify core state retention, SRAM memory persistence, and clean resume without bus lockup or clock stall.
- [ ] **4. Multi-Channel Concurrency & High-Load Stress Testing**:
  - [ ] Create multiple concurrent RPMsg channels (e.g., `rpmsg-ping-channel`, `rpmsg-telemetry`, `rpmsg-control`) multiplexed over VirtIO vrings.
  - [ ] Multi-threaded user-space stress test with 10+ concurrent worker threads hammering `/dev/rpmsg0`..`/dev/rpmsgN`.
  - [ ] Evaluate lock contention, ring buffer saturation, and PREEMPT_RT latency degradation under 100% CPU load.
- [ ] **5. Cadence Tensilica HiFi4 DSP RemoteProc Bring-Up**:
  - [ ] Add HiFi4 DSP compatible string and memory window (`0x00020000`) in `sunxi_rproc.c` / Device Tree.
  - [ ] Validate DSP clock/reset domain sequencing and firmware loading.

## Engineering record

Update `docs/platforms/CUBIE_A7A_DEBUG_LOG.md` after every target test and when any TODO item changes state. Detailed task history is also maintained at `docs/platforms/CUBIE_A7A_BRINGUP_TODO.md`.

