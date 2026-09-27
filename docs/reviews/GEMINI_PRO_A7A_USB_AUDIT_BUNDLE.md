# Radxa Cubie A7A USB 2.0 / DWC3 / FE1.1S Hub Complete System Audit Bundle
# Target: Google Gemini Pro (1.5 Pro / 2.0 Pro - 2M Context Window)

> **Instructions for Reviewer / User**:
> Copy and paste this entire document into [Google Gemini Pro](https://gemini.google.com).
> It contains the complete architectural prompt, schematic netlists, chronological debugging history,
> invariant ledgers, forensic kernel dmesg failure analysis, mainline driver code, and vendor reference drivers.

---

```text
You are an elite Linux Kernel USB Subsystem Maintainer and hardware electrical engineer specializing in Synopsys DesignWare USB3 (DWC3), UTMI+ PHY interfaces, USB 2.0 transceiver analog calibration, Linux regulator architecture, and Allwinner SoCs.

Perform an adversarial, technically uncompromising system-level audit of our mainline Linux 7.1 USB implementation for the Allwinner A733 SoC on the Radxa Cubie A7A board.

You have the complete hardware schematic netlist, chronological debugging history, negative invariant blacklist, latest silicon boot logs, mainline driver source code, and working vendor BSP reference drivers provided below in this document.

================================================================================
ARCHITECTURAL MANDATES (STRICT ENFORCEMENT)
================================================================================
1. DRIVER CHANGES ONLY FOR A733:
   - Driver C code modifications are strictly permitted ONLY in the Allwinner A733 PHY driver:
     `drivers/phy/allwinner/phy-sun60i-usb2.c`
   - FORBIDDEN: Do NOT propose changes, quirks, or patches to core generic subsystems
     (`drivers/usb/dwc3/`, `drivers/usb/host/xhci*`, `drivers/usb/core/`). Upstream maintainers
     will reject any architecture-specific hacks in core USB/DWC3 code.
2. EVERYTHING ELSE IS DEVICE TREE MODIFICATIONS FOR CUBIE A7A:
   - All board-specific configurations (GPIOs, pinmux, regulator delays, bleed-off times,
     power domains, and tuning properties) MUST be expressed cleanly as Device Tree modifications
     in `arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts`.

================================================================================
PART 1: HARDWARE SCHEMATIC NETLIST & CHIP REGISTER MANUAL REFERENCE
================================================================================
1. Physical USB Topology & Schematic Netlist (Radxa Cubie A7A V1.10):
   - Bottom USB Port (CON_U3_U2): Direct point-to-point wiring to SoC balls E36/F36 (USB1-DP/USB1-DM).
     Driven by SoC EHCI1/OHCI1 (0x04200000) and phy-sun4i-usb. 5V VBUS is switched by U2 (SGM2576) via PL2 (USB0-DRVVBUS).
     STATUS: Always works reliably (completely bypasses DWC3 and the hub).
   - Top USB Port (CON1), Internal Header (J4), and AIC8800 Wi-Fi 6 (U3):
     All wire to downstream ports 1, 2, 3, and 4 of an onboard Genesys Logic FE1.1S USB 2.0 Hub (U6).
   - Upstream FE1.1S Hub (U6):
     Pins DPU (pin 15) and DMU (pin 14) route through 0-ohm resistors R53 and R69 directly to SoC balls C36 and B37 (USB2-DP and USB2-DM).
     STATUS: Driven exclusively by SoC DWC3 (0x06A00000, Synopsys DWC3.1 IP v1.90a / IP 3331) and the Sun60i USB 2.0 Analog PHY (0x06B00000).
   - Downstream Port Routing from FE1.1S (U6):
     * Downstream Port 1 (pins 20/21, DP1/DM1): Routes to Top USB 2.0 Type-A Port (CON1).
     * Downstream Ports 2 & 3 (pins 22/23 DP2/DM2, pins 24/25 DP3/DM3): Route to Internal USB Header (J4).
     * Downstream Port 4 (pins 26/27, DP4/DM4): Routes to onboard AIC8800 Wi-Fi 6 / BT 5.2 module (U3 FCU760K, pins 13/12).

2. Power Architecture & Reset Circuits for FE1.1S Hub (U6):
   - Hub Core Power: 3.3V (VCC_3V3_USB20HUB) supplied by DCDC1 via 0-ohm resistor R58 to VDD (pin 5).
     This is a permanent board rail; it is NEVER power-gated during warm reboot or runtime.
   - Hardware Reset (XRSTJ, pin 16): There is NO SoC GPIO reset line connected to the hub. Pin 16 is tied to an RC delay network off the permanent 3.3V rail consisting of pull-up resistor R60 (10k 1%) and filter capacitor C155 (100nF 10V). Time constant: tau = 1 ms.
   - 5V VBUS Rail (VCC5V0_USB20): Switched by U5 (SGM2576 single-channel power switch).
     - Enable pin EN (pin 4) is driven by SoC GPIO PM5 (USB_HOST_EN). Active high.
     - Internal Discharge FET: SGM2576 integrates an internal ~100-ohm discharge N-MOSFET on VOUT when EN is pulled low.
     - Output VOUT (pin 1) supplies 5V to the downstream USB ports and charges a 20 uF capacitor bank (C151 10uF + C153 10uF).
     - VBUS Monitor (VBUSM, pin 17 of FE1.1S): Driven by a 50% voltage divider from VCC5V0_USB20 formed by R64 (100k 1%) and R65 (100k 1%). Hub detects VBUS valid when VBUSM >= 2.5V.
     - CRITICAL RESET MECHANISM: Because XRSTJ is tied to the permanent 3.3V rail, the FE1.1S internal Serial Interface Engine (SIE) and USB state machine reset EXCLUSIVELY when VBUSM drops below ~2.5V (i.e. VCC5V0_USB20 drops below 5V). If VBUS never drops below 2.5V, the hub NEVER undergoes a cold Power-On Reset (POR).

3. Allwinner A733 Chip Specification & Register Manual Reference:
   - Synopsys DWC3.1 Controller (0x06A00000 - 0x06AFFFFF):
     * GSNPSID: 0x06A0C120 (Reads 0x33313130 -> Synopsys DWC3.1 v1.90a, IP=3331).
     * GUCTL1:  0x06A0C11C (Bit 26: DEV_FORCE_20_CLK_FOR_30_CLK).
     * GUSB2PHYCFG(0): 0x06A0C200 (Bit 31: PHYSOFTRST, Bits [13:10]: USBTRDTIM = 9 for 8-bit UTMI 60MHz).
     * OCFG:    0x06A0CC00 (Bit 3: SFTRSTMASK prevents xHCI reset from clearing PHY/OTG state).
   - Sun60i USB 2.0 PHY (0x06B00000 - 0x06B007FF):
     * ISCR:    0x06B00000 (0x0000B000 = Force ID low, Force VBUS valid).
     * PHYCTL:  0x06B00010 (Bit 10: OTGDISABLE, Bit 5: VBUSVLDEXT, Bit 3: SIDDQ sleep, Bits [1:0]: VATESTENB eFuse factory wafer trim).
     * PHYTUNE: 0x06B00018 (Analog calibration parameter: squelch, pre-emphasis, DCAP impedance).
   - SerDes Top Configuration Subsystem (0x06C00000 - 0x06C05FFF):
     * SERDES_TOP_SUBSYS_BGR: 0x06C00008 (Bit 17: ACLK_EN, Bit 16: HCLK_EN, Bit 4: USB2P0_PHY_RSTN, Bit 21: USB3P1_ONLY_UTMI_CLK_SEL must remain 0).
   - SYSCFG Resistor Calibration (0x03000000):
     * RESCAL_CTRL: 0x03000160 (Bit 10: PCIE_USB 200-ohm trim select, Bit 0: CAL_EN must be 0 during transfers).
     * RES1_CTRL:   0x03000168 (Bits [15:8]: manual trim, cleared to allow auto-calibration baseline).
   - Main CCU (0x02002000):
     * CLK_USB2_U2_REF:  0x02003348 (24 MHz UTMI reference clock, Bit 31 gate).
     * CLK_USB2_SUSPEND: 0x02003350 (24 MHz PHY suspend clock, Bit 31 gate).
     * CLK_USB2_MF:      0x02003354 (400 MHz master transport core clock from PLL_PERIPH0).
     * RST_BUS_USB2:     0x0200335C (Bit 16: DWC3 hardware deassert reset).
     * USB2_AHB_GATE:    0x02003A00 (Bit 3: AHB bus interface gate for DWC3 MMIO).
   - Power Management & PMIC:
     * PCK-600 Power Domain 8 (PD_USB2): 0x07068000 (PPU_PWPR = 0x8 energizes domain).
     * AXP8191 PMIC ELDO4: VDD-USB 0.8V (digital core supply for DWC3).
     * AXP PMIC ALDO1: 3.3V (VCC-PL / VCC-PM I/O domain supplying PM5 and PL2).

================================================================================
PART 2: CHRONOLOGICAL DEBUGGING HISTORY & PROVEN OUTCOMES
================================================================================
We have been debugging this bringup across multiple iterations. Below is the historical ledger of what has been discovered, tested, proven, and disproven:

1. Turnaround Time (USBTRDTIM) Discovery (Sep 18, 2026):
   - Finding: An early patch set DWC3_GUSB2PHYCFG_USBTRDTIM to 15 (0x3C00). For an 8-bit UTMI PHY running at 60 MHz, USBTRDTIM must be 9 (USBTRDTIM_UTMI_8_BIT / 0x2400).
   - Setting USBTRDTIM = 15 caused the host receiver to miss device packets within the standard 8-9 cycle window. Reverted back to 9.

2. SerDes Top Bridge 0x06C00008 (SERDES_TOP_SUBSYS_BGR) Clock Mux (Sep 24, 2026):
   - Finding: Early mainline code wrote 0x00230010, which set Bit 21 (USB3P1_ONLY_UTMI_CLK_SEL).
   - Vendor BSP comparison (sunxi-cadence-combophy.c): Vendor writes 0x00030010 (USB3P1_USB2P0_PHY_RSTN | USB3P1_ACLK_EN | USB3P1_HCLK_EN), leaving Bit 21 strictly as 0.
   - Forcing Bit 21 = 1 disconnected or corrupted the 60 MHz UTMI clock generator. Read-modify-write was updated to leave Bit 21 cleared.

3. Wafer Analog Calibration Clobbering in PHYCTL (Sep 24, 2026):
   - Finding: The original mainline driver overwrote PHY_USB2_PHYCTL (0x10) with literal 0x000e2434.
   - Impact: Overwriting 0x000e2434 clobbered factory wafer trim in bits [1:0] (VATESTENB) programmed by eFuse/BROM, severely degrading transmitter eye margins.
   - Fix: Switched to strict read-modify-write setting only OTGDISABLE (bit 10) and VBUSVLDEXT (bit 5), clearing SIDDQ (bit 3).

4. Active VBUS Discharge Cycle & Bleed-Off Timing (Sep 24, 2026):
   - Finding: DTS originally had regulator-always-on and regulator-boot-on on reg_usb1_vbus.
   - Impact: Linux was forbidden from dropping PM5 low. If U-Boot left PM5 high, the hub state machine never reset.
   - Fix: Removed regulator-always-on/boot-on. Added off-on-delay-us = <200000> (200ms) to bleed the 20uF bank and startup-delay-us = <100000> (100ms) for the 12 MHz crystal to lock.

5. The Regression Cycle (Commit d258953652c4 - Sep 25, 2026):
   - An experimental commit attempted to solve remaining High-Speed handshake dropouts by bundling 6 changes into one commit:
     a) Re-introduced val |= 0x000e2434 into PHYCTL.
     b) Pulsed analog macro reset (PHYCTL_RESET, bit 0) with 20us udelay.
     c) Re-introduced regulator-always-on and regulator-boot-on into DTS.
     d) Altered SYSCFG resistor calibration to set CAL_EN = 1 and write 0xC8 into manual trim.
     e) Injected DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK into host mode.
     f) Added snps,parkmode_disable_hs_quirk to DTS.
   - Result: COMPLETE ENUMERATION FAILURE on target silicon with repeated error -71.

6. Breakthrough: Squelch Calibration & Successful 4-Port Hub Detection (Commit 155a54c4f5f2 - Sep 25, 2026):
   - Parameter sweep on live target silicon proved that `PHYTUNE = 0x143333d4` (squelch threshold = 4) successfully completed High-Speed / Full-Speed descriptors and registered the hub:
     ```text
     [ 2586.410242] hub 1-1:1.0: USB hub found
     [ 2586.410341] hub 1-1:1.0: 4 ports detected
     ```
   - This proved 100% hardware reachability of the FE1.1S hub (power, clocking, UTMI lines, EP0 control transfers).
   - CURRENT ACTIVE BLOCKER: Immediate 1.4 ms post-detection disconnect:
     ```text
     [ 2586.411742] usb usb1-port1: disabled by hub (EMI?), re-enabling...
     ```
   - Forensic Analysis: The root port disables the connection (`PORT_ENABLE` cleared by hardware) 1.4 ms after detecting the 4 downstream ports. In USB 2.0, this points to:
     a) **VBUS Inrush Sag / Brown-Out**: When the Linux hub driver configures the hub, it issues `SetPortFeature(PORT_POWER)` across all 4 downstream ports simultaneously. The inrush current charging bypass caps on downstream ports (CON1, header, and the AIC8800 Wi-Fi module) causes a transient dip on `VCC5V0_USB20`, dropping `VBUSM` below 2.5V and tripping the FE1.1S internal brown-out reset.
     b) **Analog Margin / Squelch Sensitivity**: Transient noise on the high-speed differential lines during status endpoint polling trips the xHCI babble detector or disconnect threshold.
     c) **Staggered Settling Delays**: Need settling delays for downstream regulators and PHY power rails before descriptor reading.

================================================================================
PART 3: FORENSIC LOG OF THE 4-PORT DETECTION & 1.4ms DISCONNECT
================================================================================
Below is the verbatim dmesg log from the target hardware running Linux 7.1 with `PHYTUNE = 0x143333d4`:

```text
[ 2586.284413] xhci-hcd xhci-hcd.0.auto: xHCI Host Controller
[ 2586.284426] xhci-hcd xhci-hcd.0.auto: new USB bus registered, assigned bus number 1
[ 2586.285549] hub 1-0:1.0: USB hub found
[ 2586.285562] hub 1-0:1.0: 1 port detected
[ 2586.350100] usb 1-1: new high-speed USB device number 2 using xhci-hcd
[ 2586.410242] hub 1-1:1.0: USB hub found
[ 2586.410341] hub 1-1:1.0: 4 ports detected
[ 2586.411742] usb usb1-port1: disabled by hub (EMI?), re-enabling...
[ 2587.620010] usb 1-1: USB disconnect, device number 2
```
Bus 003 Device 001: ID 1d6b:0002 Linux 7.1.0 ehci_hcd EHCI Host Controller
Bus 001 Device 001: ID 1d6b:0002 Linux 7.1.0 xhci-hcd xHCI Host Controller
Bus 006 Device 001: ID 1d6b:0001 Linux 7.1.0 ohci_hcd Generic Platform OHCI controller
Bus 004 Device 001: ID 1d6b:0002 Linux 7.1.0 ehci_hcd EHCI Host Controller
Bus 002 Device 001: ID 1d6b:0003 Linux 7.1.0 xhci-hcd xHCI Host Controller
```

### Forensic Code Path Discovery:
1. At 1.524s, the hub is detected as High-Speed:
   `dev_info(&udev->dev, "%s %s USB device number %d using %s\n", ...)` prints `new high-speed USB device number 2`.
2. Inside `hub_port_init()` (drivers/usb/core/hub.c lines 5056-5064):
   ```c
   retval = hub_port_reset(hub, port1, udev, delay, false);
   if (retval < 0)
       goto fail;
   if (oldspeed != udev->speed) {
       dev_dbg(&udev->dev, "device reset changed speed!\n");
       retval = -ENODEV;
       goto fail;
   }
   ```
3. During `hub_port_reset()`, the hub fails the High-Speed handshake (Chirp K / Chirp J) and drops to Full-Speed.
4. Because `oldspeed (USB_SPEED_HIGH) != udev->speed (USB_SPEED_FULL)`, `hub_port_init` exits silently to `fail` without printing an error (because `dev_dbg` is disabled).
5. At 3.317s, the hub retry logic reallocates the device as Full-Speed Device 3. In Full-Speed mode on this DWC3 host, all subsequent descriptor reads time out or fail with protocol error `-71`.

### Empirical Silicon Test of Gemini Pro Proposal (0x143338d6 + AXI Flushes - Sep 27, 2026):
We compiled and executed Gemini Pro's exact patch on target hardware with Build #2:
```text
[ 0.000000] Kernel command line: console=ttyS0,115200 earlycon=uart8250,mmio32,0x02500000 root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8 keep_bootcon clk_ignore_unused fw_devlink=pe1
...
[ 1.018579] sun60i-a733-usb2-phy 6b00000.phy: Allwinner A733 USB 2.0 PHY probed at [mem 0x06b00000-0x06b007ff flags 0x200] (tune=0x143338d6)
[ 1.639460] phy phy-6b00000.phy.2: A733 USB2 PHY initialized (tune=0x143338d6)
[ 1.927440] usb 1-1: new high-speed USB device number 2 using xhci-hcd
[ 1.927491] usb 1-1: Device not responding to setup address.
[ 2.135471] usb 1-1: Device not responding to setup address.
[ 2.343436] usb 1-1: device not accepting address 2, error -71
...
[ 3.720459] usb 1-1: new full-speed USB device number 4 using xhci-hcd
[ 3.733720] hub 1-1:1.0: config failed, can't read hub descriptor (err -22)
[ 3.744669] usb usb1-port1: disabled by hub (EMI?), re-enabling...
[ 3.744681] usb 1-1: USB disconnect, device number 4
```
CRITICAL VERIFIED TAKEAWAYS:
1. SQUELCH 0x143338d6 FAILS ON HARDWARE: Squelch threshold 0x6 is too insensitive for the PCB attenuation. It drops EP0 packets immediately (`Device not responding to setup address`), whereas `0x143333d4` (squelch threshold 0x4) successfully enumerated the 4 ports. Squelch sensitivity must remain at 0x4.
2. U-BOOT BOOTARGS TRUNCATION: Notice `Kernel command line` is truncated at `... fw_devlink=pe1`. The string `usbcore.old_scheme_first=1` was dropped by U-Boot! Linux defaulted to the new scheme, which forces a mid-stream port reset after 8 bytes and triggers transaction error -71.

================================================================================
PART 4: THE INVARIANT LEDGER & NEGATIVE INVARIANT BLACKLIST
================================================================================
Any solution or recommendation MUST strictly satisfy the following invariants:

1. [INVARIANT 1]: `reg_usb1_vbus` MUST NOT have `regulator-always-on` or `regulator-boot-on`.
   - Proof: The FE1.1S hub core 3.3V is permanently powered. The hub reset state machine triggers exclusively when VBUS drops below ~2.5V via the VBUSM pin.
2. [INVARIANT 2]: `PHY_USB2_PHYCTL` (`0x10`) MUST be modified using read-modify-write ONLY.
   - Proof: Overwriting with `0x000e2434` destroys factory wafer analog calibration trim in bits [1:0] (`VATESTENB`).
3. [INVARIANT 3]: Resistor Auto-Calibration in SYSCFG (`0x03000160`/`168`) MUST match vendor V2 mode.
   - Proof: Vendor driver `phy_rescal_set_v2` sets `PCIE_USB_RES200_TRIM_SEL` (`bit 10`), CLEARS `CAL_EN` (`bit 0`), and CLEARS manual trim bits [15:8]. Leaving `CAL_EN=1` running modulates the 45-ohm termination resistors during active data transfers.
4. [INVARIANT 4]: Host Mode Soft Reset in `dwc3_core_soft_reset()` MUST pulse `PHYSOFTRST` and sleep 50ms.
   - Proof: Matches vendor BSP `core.c` lines 294-318. Required to phase-align the UTMI 60 MHz elastic FIFO.
5. [INVARIANT 5]: DO NOT set `DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK` in host mode.
   - Proof: Synopsys Databook explicitly documents this as a Device-Mode-only bit. Setting it in host mode creates undefined controller states.
6. [INVARIANT 6]: MMIO Posted-Write Flush Before Reset / Deassert.
   - Proof: On ARM64 AXI/AHB interconnects, register writes (`writel`) are posted. Any write to `PHYCTL`, `PHYTUNE`, or `SERDES_TOP_SUBSYS_BGR` that alters clock/reset gating must be followed by a dummy `readl()` read-back to guarantee the write has landed before deasserting resets or beginning delay timers.
7. [INVARIANT 7]: Downstream Inrush Current & Settling Mitigation.
   - Proof: When the hub is enumerated, the Linux hub driver powers on all downstream ports simultaneously. The simultaneous inrush current charging downstream bypass capacitors (especially the AIC8800 module and external ports) can cause a transient sag on `VCC5V0_USB20` below the FE1.1S 2.5V brownout threshold, tripping the 1.4ms disconnect. Settling delays and power-on debounce margins must be strictly respected.
8. [INVARIANT 8]: Teardown & PM Lifecycle Symmetry.
   - Proof: In `sun60i_usb2_phy_exit()` and system suspend, transceiver power must be gated (`PHYCTL_SIDDQ` asserted) and clocks turned off in reverse LIFO order to prevent bus contention or state corruption on unbind.

================================================================================
PART 5: COMPLETE VERBATIM CODE & VENDOR REFERENCE ATTACHMENTS
================================================================================

--- FILE: drivers/phy/allwinner/phy-sun60i-usb2.c (Mainline Linux 7.1) ---
```c
// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner A733 (sun60iw2) USB 2.0 PHY driver for DWC3
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define SUN60I_DEFAULT_PHY_TUNE	0x143333d4

struct sun60i_usb2_phy {
	void __iomem *base;
	struct regulator *vbus;
	u32 tune_param;
};

#define PHY_USB2_ISCR		0x00
#define PHY_USB2_PHYCTL		0x10
#define PHY_USB2_PHYTUNE	0x18
#define SERDES_TOP_SUBSYS_BGR	0x06c00008

static void sun60i_usb2_phy_hw_init(struct sun60i_usb2_phy *priv)
{
	void __iomem *subsys_bgr;
	u32 val;

	/*
	 * Deassert PHY reset and enable ACLK/HCLK in SerDes top bridge.
	 * Strictly enables ACLK_EN, HCLK_EN, and USB2P0_PHY_RSTN via read-modify-write
	 * matching vendor combo_usb2_clk_set / combo_usb_clk_set without touching Bit 21.
	 */
	subsys_bgr = ioremap(SERDES_TOP_SUBSYS_BGR, 4);
	if (subsys_bgr) {
		val = readl(subsys_bgr);
		val |= BIT(17) | BIT(16) | BIT(4); /* ACLK_EN, HCLK_EN, USB2P0_PHY_RSTN */
		writel(val, subsys_bgr);
		iounmap(subsys_bgr);
	}

	/* Configure 200-ohm resistor calibration in SYSCFG (0x03000000) */
	{
		void __iomem *syscfg = ioremap(0x03000160, 0x10);

		if (syscfg) {
			/*
			 * RESCAL_CTRL (0x160): select PCIE_USB 200 ohm trim
			 * (bit 10), clear CAL_EN (bit 0).
			 */
			val = readl(syscfg + 0x00);
			val &= ~BIT(0);
			val |= BIT(10);
			writel(val, syscfg + 0x00);

			/* RES1_CTRL (0x168): clear manual trim bits [15:8] for auto-calibration */
			val = readl(syscfg + 0x08);
			val &= ~GENMASK(15, 8);
			writel(val, syscfg + 0x08);

			iounmap(syscfg);
		}
	}

	/* Force ID low and VBUS valid in ISCR to guarantee host mode */
	writel(0x0000b000, priv->base + PHY_USB2_ISCR);

	/*
	 * Clear SIDDQ (bit 3) and set OTGDISABLE (bit 10) | VBUSVLDEXT (bit 5) in PHYCTL
	 * using read-modify-write to preserve factory analog calibration trim.
	 */
	val = readl(priv->base + PHY_USB2_PHYCTL);
	val |= BIT(10) | BIT(5);
	val &= ~BIT(3);
	writel(val, priv->base + PHY_USB2_PHYCTL);

	/* Apply analog tuning (squelch threshold, pre-emphasis, DCAP) */
	writel(priv->tune_param, priv->base + PHY_USB2_PHYTUNE);
}

static int sun60i_usb2_phy_init(struct phy *phy)
{
	struct sun60i_usb2_phy *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->vbus) {
		/*
		 * If U-Boot or prior boot stage left PM5 (VBUS) high,
		 * cycle the regulator off to force a clean Power-On Reset.
		 * The Linux regulator core automatically enforces off-on-delay-us
		 * (200ms) to bleed the 20uF capacitor bank before asserting PM5,
		 * and startup-delay-us (100ms) for the crystal to settle.
		 */
		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;

		ret = regulator_disable(priv->vbus);
		if (ret)
			return ret;

		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;
	}

	sun60i_usb2_phy_hw_init(priv);
	dev_info(&phy->dev, "A733 USB2 PHY initialized (tune=0x%08x)\n", priv->tune_param);
	return 0;
}

static int sun60i_usb2_phy_exit(struct phy *phy)
{
	struct sun60i_usb2_phy *priv = phy_get_drvdata(phy);
	u32 val;

	val = readl(priv->base + PHY_USB2_PHYCTL);
	val &= ~(BIT(10) | BIT(5));
	val |= BIT(3); /* Assert SIDDQ */
	writel(val, priv->base + PHY_USB2_PHYCTL);

	if (priv->vbus)
		regulator_disable(priv->vbus);

	return 0;
}

static const struct phy_ops sun60i_usb2_phy_ops = {
	.init		= sun60i_usb2_phy_init,
	.exit		= sun60i_usb2_phy_exit,
	.owner		= THIS_MODULE,
};

static int sun60i_usb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sun60i_usb2_phy *priv;
	struct phy_provider *provider;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(priv->vbus)) {
		if (PTR_ERR(priv->vbus) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		priv->vbus = NULL;
	}

	if (of_property_read_u32(dev->of_node, "aw,phy_tune_param", &priv->tune_param))
		priv->tune_param = SUN60I_DEFAULT_PHY_TUNE;

	pm_runtime_enable(dev);

	phy = devm_phy_create(dev, NULL, &sun60i_usb2_phy_ops);
	if (IS_ERR(phy)) {
		pm_runtime_disable(dev);
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, priv);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		pm_runtime_disable(dev);
		return PTR_ERR(provider);
	}

	/* Initialize hardware registers and ungate SerDes bus bridge for DWC3 GSNPSID */
	sun60i_usb2_phy_hw_init(priv);

	dev_info(dev, "Allwinner A733 USB 2.0 PHY probed at %pr (tune=0x%08x)\n",
		 platform_get_resource(pdev, IORESOURCE_MEM, 0), priv->tune_param);

	return 0;
}

static const struct of_device_id sun60i_usb2_phy_of_match[] = {
	{ .compatible = "allwinner,sun60i-a733-usb2-phy" },
	{ .compatible = "allwinner,sunxi-plat-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun60i_usb2_phy_of_match);

static struct platform_driver sun60i_usb2_phy_driver = {
	.probe	= sun60i_usb2_phy_probe,
	.driver	= {
		.name		= "sun60i-a733-usb2-phy",
		.of_match_table	= sun60i_usb2_phy_of_match,
	},
};
module_platform_driver(sun60i_usb2_phy_driver);

MODULE_DESCRIPTION("Allwinner A733 USB 2.0 PHY driver");
MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_LICENSE("GPL");
```

--- FILE: arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts Excerpts ---
```dts
	reg_usb1_vbus: usb1-vbus {
		compatible = "regulator-fixed";
		regulator-name = "usb1-vbus";
		regulator-min-microvolt = <5000000>;
		regulator-max-microvolt = <5000000>;
		gpios = <&r_pio 1 5 GPIO_ACTIVE_HIGH>; /* PM5 (USB_HOST_EN) */
		enable-active-high;
		off-on-delay-us = <200000>; /* 200ms to bleed 20uF capacitor bank */
		startup-delay-us = <100000>; /* 100ms for FE1.1S 12MHz crystal to settle */
		status = "okay";
	};

	u2phy: phy@6b00000 {
		compatible = "allwinner,sun60i-a733-usb2-phy";
		reg = <0x06b00000 0x800>;
		power-domains = <&pck600 PD_USB2>;
		vbus-supply = <&reg_usb1_vbus>;
		aw,phy_tune_param = <0x143333d4>;
		#phy-cells = <0>;
		status = "okay";
	};

	dwc3: usb@6a00000 {
		compatible = "snps,dwc3";
		reg = <0x06a00000 0x100000>;
		interrupts = <GIC_SPI 155 IRQ_TYPE_LEVEL_HIGH>;
		clocks = <&ccu CLK_USB2_MF>,
			 <&ccu CLK_USB2_U2_REF>,
			 <&ccu CLK_USB2_SUSPEND>;
		clock-names = "bus_clk", "ref", "suspend";
		assigned-clocks = <&ccu CLK_USB2_SUSPEND>;
		assigned-clock-rates = <24000000>;
		resets = <&ccu RST_BUS_USB2>;
		power-domains = <&pck600 PD_USB2>;
		phys = <&u2phy>;
		phy-names = "usb2-phy";
		dr_mode = "host";
		maximum-speed = "high-speed";
		phy_type = "utmi";
		snps,dis_enblslpm_quirk;
		snps,dis-u1-entry-quirk;
		snps,dis-u2-entry-quirk;
		snps,dis_u3_susphy_quirk;
		snps,dis_u2_susphy_quirk;
		snps,usb2-lpm-disable;
		status = "okay";
	};
```

--- FILE: Vendor Reference: A7A_kernel/linux-a733/bsp/drivers/usb/dwc3/phy-sunxi-plat.c ---
```c
static void phy_rescal_set_v2(struct sunxi_phy *phy, bool enable)
{
	__u32 val = 0;
	__u32 port = 0, tmp;

	tmp = GENMASK(5, 4);
	val = readl(CFG_REG_RESCAL_CTRL(phy->res));
	port = val & tmp;
	if (enable) {
		val &= ~CAL_EN;
		val |= PCIE_USB_RES200_TRIM_SEL;
	} else {
		if (port == 0)
			val |= CAL_EN;
		val &= ~PCIE_USB_RES200_TRIM_SEL;
	}
	writel(val, CFG_REG_RESCAL_CTRL(phy->res));

	val = readl(CFG_REG_RES1_CTRL(phy->res));
	if (enable)
		val &= ~PCIE_USB_RES200_TRIM;
	else
		val |= PCIE_USB_RES200_TRIM_DEFAULT;
	writel(val, CFG_REG_RES1_CTRL(phy->res));
}

static void phy_u2_set(struct sunxi_phy *phy, bool enable)
{
	u32 val, tmp = 0;

	val = readl(PHY_REG_U2_PHYCTL(phy->u2));
	if (phy->drvdata->has_vbusvldext)
		tmp = OTGDISABLE | VBUSVLDEXT;
	if (enable) {
		val |= tmp;
		val &= ~SIDDQ; /* write 0 to enable phy */
	} else {
		val &= ~tmp;
		val |= SIDDQ; /* write 1 to disable phy */
	}

	writel(val, PHY_REG_U2_PHYCTL(phy->u2));
}

static int sunxi_phy_init(struct phy *_phy)
{
	struct sunxi_phy *phy = phy_get_drvdata(_phy);
	int ret;

	if (!IS_ERR(phy->reset)) {
		ret = reset_control_deassert(phy->reset);
		if (ret)
			return ret;
	}

	if (!IS_ERR(phy->clk)) {
		ret = clk_prepare_enable(phy->clk);
		if (ret)
			return ret;
	}

	phy_res_set(phy, true);
	phy_u2_set(phy, true);

	if (phy->param != PHY_TUNE_PARAM_INVALID)
		writel(phy->param, PHY_REG_U2_PHYTUNE(phy->u2));

	return 0;
}
```

--- FILE: Vendor Reference: A7A_kernel/linux-a733/bsp/drivers/phy/sunxi-cadence-combophy.c ---
```c
static void combo_usb2_clk_set(struct sunxi_cadence_phy *sunxi_cphy, bool enable)
{
	u32 val;

	val = readl(SUBSYS_REG_USB3BGR(sunxi_cphy->top_subsys_reg));
	if (enable)
		val |= USB3P1_USB2P0_PHY_RSTN; // BIT(4)
	else
		val &= ~USB3P1_USB2P0_PHY_RSTN;
	writel(val, SUBSYS_REG_USB3BGR(sunxi_cphy->top_subsys_reg));
}

static void combo_usb_clk_set(struct sunxi_cadence_phy *sunxi_cphy, bool enable)
{
	u32 val, tmp = 0;

	val = readl(SUBSYS_REG_USB3BGR(sunxi_cphy->top_subsys_reg));
	tmp = USB3P1_ACLK_EN | USB3P1_HCLK_EN; // BIT(17) | BIT(16)
	if (enable)
		val |= tmp;
	else
		val &= ~tmp;
	writel(val, SUBSYS_REG_USB3BGR(sunxi_cphy->top_subsys_reg));
}
```

--- FILE: Vendor Extracted DTS: cubie-a5e/docs/extracted_vendor/sun60i-a733-cubie-a7a.dts ---
```dts
	usb1-vbus {
		compatible = "regulator-fixed";
		regulator-name = "usb1-vbus";
		regulator-min-microvolt = <0x4c4b40>; /* 5000000 */
		regulator-max-microvolt = <0x4c4b40>; /* 5000000 */
		regulator-enable-ramp-delay = <0x3e8>; /* 1000 us */
		gpio = <0x35 0x01 0x05 0x00>; /* PM5 */
		enable-active-high;
		phandle = <0x9c>;
	};

	xhci2-controller@6a00000 {
		compatible = "snps,dwc3";
		reg = <0x00 0x6a00000 0x00 0x100000>;
		interrupts = <0x00 0x9b 0x04>;
		dr_mode = "host";
		clocks = <0x12 0xe0 0x12 0xde 0x12 0xdf>;
		clock-names = "bus_clk", "ref_clk", "suspend";
		assigned-clocks = <0x12 0xdf>;
		assigned-clock-rates = <0x16e3600>;
		resets = <0x12 0x58>;
		reset-names = "hci";
		power-domains = <0x13 0x08>;
		maximum-speed = "super-speed-plus";
		phy_type = "utmi";
		snps,dis_enblslpm_quirk;
		snps,dis-u1-entry-quirk;
		snps,dis-u2-entry-quirk;
		snps,dis_u3_susphy_quirk;
		snps,dis_u2_susphy_quirk;
		phys = <0x9d 0x9e>;
		phy-names = "usb2-phy", "usb3-phy";
		status = "okay";
		drvvbus-supply = <0x9c>;
	};

	phy@6b00000 {
		compatible = "allwinner,sunxi-plat-phy";
		reg = <0x00 0x6b00000 0x00 0x800 0x00 0x3000000 0x00 0x300>;
		reg-names = "u2_base", "res_base";
		clocks = <0x12 0x10b>;
		clock-names = "res_dcap";
		aw,rext_mode = <0x02>;
		aw,phy_tune_param = <0x143338d6>;
		#phy-cells = <0x00>;
		status = "okay";
		phandle = <0x9d>;
	};
```

================================================================================
PART 6: TARGET ADVERSARIAL AUDIT QUESTIONS FOR GEMINI PRO
================================================================================
Evaluate all code, schematics, and traces with ZERO tolerance for speculation:

1. Squelch Parameter Reality Check (`0x143338d6` vs `0x143333d4`):
   - We tested `0x143338d6` on silicon with the AXI flushes. It completely failed EP0 setup address assignment (`Device not responding to setup address`), dropping straight to Full-Speed.
   - In contrast, `0x143333d4` successfully read EP0 descriptors and detected the 4 ports.
   - Explain why the less sensitive squelch (`0x6`) in `0x143338d6` is unable to detect packet responses on this board, whereas `0x4` succeeded.

2. Resolving the 1.4ms Post-Enumeration Disconnect with `0x143333d4`:
   - With `0x143333d4` detecting all 4 downstream ports, what exact mechanism trips the immediate 1.4ms disconnect (`usb usb1-port1: disabled by hub (EMI?), re-enabling...`)?
   - Is it the downstream inrush current causing a transient sag on VBUSM below 2.5V, or is it an xHCI babble timeout on the status interrupt pipe?

3. U-Boot Command Line Truncation:
   - Notice `bootargs` was truncated at `fw_devlink=pe1`, dropping `usbcore.old_scheme_first=1`.
   - How can we best format the kernel command line in U-Boot (`boot.cmd`) to guarantee `usbcore.old_scheme_first=1` is passed without exceeding U-Boot buffer limits?

4. Single-Variable Patch Proposal:
   - Provide the EXACT, minimal diff to `drivers/phy/allwinner/phy-sun60i-usb2.c` and `sun60i-a733-cubie-a7a.dts` to eliminate the 1.4ms post-enumeration disconnect while retaining `0x143333d4`.
```
