# Allwinner A733 USB Bringup & Hardware Invariants Rule

This rule governs all work on the Allwinner A733 USB subsystem (DWC3, USB 2.0 PHY, and FE1.1S Hub) on the Radxa Cubie A7A.
It prevents regression, eliminates "shotgun debugging", and keeps both AI assistants and human developers anchored to verified hardware invariants.

---

## 1. Absolute Hardware Invariants (NEVER VIOLATE)

Any proposal, patch, or commit that violates these invariants is **automatically rejected**:

1. **VBUS Regulator Dynamic Control (`reg_usb1_vbus`)**:
   - `reg_usb1_vbus` MUST NOT have `regulator-always-on` or `regulator-boot-on`.
   - **Reason**: The Genesys Logic FE1.1S USB hub (`U6`) reset pin `XRSTJ` is tied to permanent 3.3V (`DCDC1`). The hub resets its state machine **exclusively via its VBUS monitor pin (`VBUSM`, pin 17)** when 5V VBUS drops below ~2.5V. If VBUS is marked `always-on`, Linux can never drop `PM5` low to reset the hub.
   - `off-on-delay-us = <200000>` (200 ms) and `startup-delay-us = <100000>` (100 ms) must remain in DTS.

2. **PHY Control Register (`PHY_USB2_PHYCTL`, 0x10)**:
   - MUST use **read-modify-write ONLY**: setting `OTGDISABLE` (`bit 10`) and `VBUSVLDEXT` (`bit 5`), and clearing `SIDDQ` (`bit 3`).
   - **FORBIDDEN**: Never write raw literal `0x000e2434` or OR `0x000e2434`. This clobbers factory wafer analog calibration trim (bits [1:0] `VATESTENB`) programmed by eFuse/BROM, destroying transceiver eye margins.
   - **FORBIDDEN**: Never pulse `PHYCTL_RESET` (bit 0) while UTMI clocks are running.

3. **SYSCFG Resistor Calibration (`0x03000160` / `0x03000168`)**:
   - Must strictly match vendor V2 mode:
     - `RESCAL_CTRL` (`0x160`): Clear `CAL_EN` (`bit 0 = 0`), set `PCIE_USB_RES200_TRIM_SEL` (`bit 10 = 1`).
     - `RES1_CTRL` (`0x168`): Clear manual trim bits [15:8] (`val &= ~GENMASK(15, 8)`).
   - **FORBIDDEN**: Never leave `CAL_EN` (`bit 0`) asserted. Continuous calibration modulates the 45-ohm termination resistors during active packet transfers, causing CRC and packet framing errors.

4. **DWC3 Host-Mode IP Quirks**:
   - **FORBIDDEN**: Never set `DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK` in host mode. The `DEV_` prefix denotes this as a Device-Mode-only quirk per the Synopsys Databook.
   - **FORBIDDEN**: Never add speculative parkmode or LPM quirks without documented hardware errata justification.
   - `dwc3_core_soft_reset()`: MUST pulse `DWC3_GUSB2PHYCFG_PHYSOFTRST` and sleep 50 ms when `dr_mode == USB_DR_MODE_HOST` to phase-align the UTMI 60 MHz clock.

---

## 2. Single-Variable Scientific Debugging Protocol

1. **One Hypothesis, One Variable Per Commit**:
   - Every commit or patch must touch **exactly ONE variable** (e.g. test squelch threshold `PHYTUNE = 0x143338D0` vs `0x143338D6`).
   - Commits modifying multiple domains (e.g., regulator + DWC3 quirk + PHYCTL reset + resistor cal) are strictly prohibited.
2. **Lean Verification Log**:
   - Do NOT paste entire 500-line minicom dmesg logs into conversations.
   - Paste ONLY the 10 lines containing `usb 1-1:` and any driver error returns.
3. **Verify State Before Modifying**:
   - Before proposing code changes, cross-reference against:
     - `cubie-a5e/docs/platforms/CUBIE_A7A_DEBUG_LOG.md`
     - `cubie-a5e/docs/reviews/GEMINI_PRO_A7A_USB_AUDIT_BUNDLE.md`
     - Vendor reference: `A7A_kernel/linux-a733/bsp/drivers/usb/dwc3/phy-sunxi-plat.c`
