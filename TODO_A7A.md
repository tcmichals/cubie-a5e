# Radxa Cubie A7A (Allwinner A733) - Bring-Up Roadmap

This document tracks completed milestones, active diagnostic tasks, and next steps for the **Radxa Cubie A7A** (Allwinner A733 octa-core ARM Cortex-A76/A55, XuanTie E902 co-processor @ 200 MHz, Maxio MAE0621A Gigabit Ethernet, FE1.1S USB Hub, and AIC8800 Wi-Fi 6).

---

## 1. Hardware Overview & Current Bring-Up Status

| Subsystem | Silicon / Component | Status | Next Milestone |
| :--- | :--- | :--- | :--- |
| **SoC / Bootloader** | Allwinner A733 / U-Boot 2024 / Linux 7.1 | **Operational** | Clean Buildroot patch gate |
| **USB & Hub** | FE1.1S Hub + AIC8800 Wi-Fi 6 | **Active Bring-Up** | Hub & Wi-Fi device enumeration on target |
| **Ethernet** | GMAC210 + Maxio MAE0621A-Q3C | **Active Blocker** | Resolve TX DMA watchdog timeout (multi-MSI) |
| **Co-Processor** | XuanTie E902 (200 MHz, RV32EMC) | **Architecture Defined** | Dual-mode RemoteProc bring-up (Mode 2) |
| **PMIC / Regulators** | AXP717 / GPIO switches (PL2, PM5, PM0, PM1) | **Configured** | Voltage verification under load |

---

## 2. USB and Power Subsystem (Active Priority)

* **Goal**: Enable the onboard FE1.1S USB 2.0 4-port hub on Host 1 (`ehci1`) and verify enumeration of the integrated AIC8800 Wi-Fi 6 / BT 5.4 module on hub downstream port 4.

- [x] **Schematic & Power Sequencing Analysis**:
  - [x] Decoded V1.10 schematic sheets 4, 13–15, and 18.
  - [x] Configured GPIO power switches as `regulator-always-on` and `regulator-boot-on`:
    - Port 0 VBUS: `PL2` (`USB0-DRVVBUS`)
    - Port 1 / Hub VBUS: `PM5` (`USB_HOST_EN`)
    - Wi-Fi Power: `PM0` (`WL_REG_ON` / 3.3V power gate)
    - Wi-Fi Chip Enable: `PM1` (`WL_WAKE_AP` / reset-enable)
- [x] **CCU Interconnect & HCI Clocks**:
  - [x] Un-gated `0x05C0` (`AHB_GATE_SW_CFG` bit 9) in CCU probe.
  - [x] Updated `bus_usb0_clk` and `bus_usb1_clk` to mask `BIT(4) | BIT(0)` (`0x1304`/`0x130c`), clocking EHCI DMA engines and OHCI.
- [x] **PHY SIDDQ & Shared Resets**:
  - [x] Added `sun60i_a733_cfg` in `phy-sun4i-usb.c` clearing `PHY_CTL_SIDDQ | PHY_CTL_H3_SIDDQ` on PMU1.
  - [x] Switched to `devm_reset_control_get_shared()` to prevent `-EBUSY` collisions between USB host controllers.
- [ ] **Target Hardware Validation**:
  - [ ] Boot newly compiled Linux kernel image on Radxa Cubie A7A hardware.
  - [ ] Verify `dmesg | grep -i -E "ehci|ohci|phy-sun4i"` shows clean probe without `-EBUSY`.
  - [ ] Run `lsusb` to confirm FE1.1S 4-port USB 2.0 hub enumerates (`1a40:0101`).
  - [ ] Confirm AIC8800 Wi-Fi 6 device enumerates on hub downstream port 4 (`0xA69C:0x8800`).
  - [ ] Load `aic8800_fdrv` out-of-tree kernel driver and verify `wlan0` interface appears.

---

## 3. Ethernet Subsystem (Active Blocker: TX DMA Timeout)

* **Goal**: Establish stable, bidirectional Gigabit Ethernet connectivity on `eth0` without TX queue watchdog timeouts.

- [x] **PHY & Link Discovery (Completed)**:
  - [x] MDIO discovers Maxio MAE0621A-Q3C at PHY address 1.
  - [x] U-Boot target proof: pings to live peer `192.168.1.2` succeeded (proves magnetics, cable, RGMII routing, and PHY reset/power are physically intact).
  - [x] Target Linux boot establishes link: `Link is Up - 1Gbps/Full`.
  - [x] Added `ethtool` (`/usr/sbin/ethtool`) and `phytool` (`/usr/bin/phytool`) to target image.
- [x] **Failure Mode Identified**:
  - [x] Repeated console message: `NETDEV WATCHDOG: transmit queue 0 timed out` every 5–6 seconds.
  - [x] TX byte count stalls at ~1,314 bytes with 0 RX bytes received; adapter continuously resets and re-negotiates link.
- [ ] **Diagnosis & Driver Fix**:
  - [ ] Capture `/proc/interrupts` before and after traffic to see if only `macirq` triggers while queue IRQs remain 0.
  - [ ] Vendor GMAC210 driver uses split multi-MSI with dedicated TX0/RX0 IRQs; mainline `dwmac-sun55i` currently lacks `STMMAC_FLAG_MULTI_MSI_EN`.
  - [ ] Add standard queue IRQ names in DTS and enable multi-MSI in A733 stmmac glue driver.
  - [ ] Retest ping on target silicon and confirm TX DMA packet retirement.

---

## 4. Co-Processor & Real-Time Control: A733 Dual-Mode Architecture

* **Goal**: Support XuanTie E902 (RV32EMC @ 200 MHz, no FPU, 208 KB System SRAM A2) co-processor execution via Linux RemoteProc.

- [x] **Silicon & Security Discovery**:
  - [x] Identified co-processor as XuanTie E902 (distinct from A5E's E907).
  - [x] Stock BL31 write-protects `0x07032204` for factory `scp.fex`.
- [x] **Dual-Mode Operating Model**:
  - **Mode 1 (Suspend/Resume)**: Stock TOC1 with `scp.fex` for consumer S3 deep sleep.
  - **Mode 2 (Real-Time Control / Linux RemoteProc)**: 24/7 industrial real-time control without suspend/resume.
- [ ] **Mode 2 Execution Tasks (Post-USB Bring-Up)**:
  - [ ] **TF-A (BL31)**: Configure `sunxi_security.c` to unlock `R_SPC` (`0x07002000`) and `R_TZMA` (`0x07003000`) so Non-Secure Linux EL1 can access `0x07032000` and System SRAM A2 (`0x00040000`).
  - [ ] **U-Boot**: Ensure U-Boot RSB driver powers PMIC `DCDC1` and `ALDO1` when `scp.fex` is omitted from TOC1.
  - [ ] **Device Tree**: Add `cubie-a7a-rproc.dtso` overlay defining `&rproc` and `0x4E000000` DMA carveout pool.
  - [ ] **Kernel Driver**: Update `sunxi_rproc.c` with `"allwinner,sun60i-a733-rproc"` to map SRAM A2 (`0x00040000`, 208 KB) and manage E902 lifecycle.

---

## 5. Mandatory Patch Gate & Engineering Records

- [x] Clean Buildroot validation gate passing: `make -C bld.a7a linux-dirclean && make -C bld.a7a linux`.
- [ ] Rerun clean validation gate before committing any new A7A patch.
- [ ] Keep permanent kernel patches in `project-cubie-a5e/patches/linux/` (never leave fixes isolated in `bld.a7a/`).
- [ ] Maintain diagnostic record in `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.
