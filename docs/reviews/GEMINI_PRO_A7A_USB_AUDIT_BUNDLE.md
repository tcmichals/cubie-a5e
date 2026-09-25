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
PART 1: HARDWARE & SCHEMATIC GROUND TRUTH (Radxa Cubie A7A V1.10)
================================================================================
1. Physical USB Topology:
   - Bottom USB Port (CON_U3_U2): Direct point-to-point wiring to SoC balls E36/F36 (USB1-DP/USB1-DM).
     Driven by SoC EHCI1/OHCI1 (0x04200000) and phy-sun4i-usb. 5V VBUS is switched by U2 (SGM2576) via PL2 (USB0-DRVVBUS).
     STATUS: Always works reliably (completely bypasses DWC3 and the hub).
   - Top USB Port (CON1), Internal Header (J4), and AIC8800 Wi-Fi 6 (U3):
     All wire to downstream ports 1, 2, 3, and 4 of an onboard Genesys Logic FE1.1S USB 2.0 Hub (U6).
   - Upstream FE1.1S Hub (U6):
     Pins DPU (pin 15) and DMU (pin 14) route through 0-ohm resistors R53 and R69 directly to SoC balls C36 and B37 (USB2-DP and USB2-DM).
     STATUS: Driven exclusively by SoC DWC3 (0x06A00000, Synopsys DWC3.1 IP v1.90a) and the Sun60i USB 2.0 Analog PHY (0x06B00000).

2. Power Architecture & Reset Circuits for FE1.1S Hub (U6):
   - Hub Core Power: 3.3V (VCC_3V3_USB20HUB) supplied by DCDC1 via 0-ohm resistor R58. This is a permanent board rail; it is NEVER power-gated during warm reboot or runtime.
   - Hardware Reset (XRSTJ, pin 16): There is NO SoC GPIO reset line connected to the hub. Pin 16 is tied to an RC delay network off the permanent 3.3V rail consisting of pull-up resistor R60 (10k 1%) and filter capacitor C155 (100nF 10V). Time constant: tau = 1 ms.
   - 5V VBUS Rail (VCC5V0_USB20): Switched by U5 (SGM2576 power switch).
     - Enable pin EN (pin 4) is driven by SoC GPIO PM5 (USB_HOST_EN). Active high.
     - Internal Discharge FET: SGM2576 integrates an internal ~100-ohm discharge N-MOSFET on VOUT when EN is pulled low.
     - Output VOUT (pin 1) supplies 5V to the downstream USB ports and charges a 20 uF capacitor bank (C151 10uF + C153 10uF).
     - VBUS Monitor (VBUSM, pin 17 of FE1.1S): Driven by a 50% voltage divider from VCC5V0_USB20 formed by R64 (100k 1%) and R65 (100k 1%). Hub detects VBUS valid when VBUSM >= 2.5V.
     - CRITICAL RESET MECHANISM: Because XRSTJ is tied to the permanent 3.3V rail, the FE1.1S internal Serial Interface Engine (SIE) and USB state machine reset EXCLUSIVELY when VBUSM drops below ~2.5V (i.e. VCC5V0_USB20 drops below 5V). If VBUS never drops below 2.5V, the hub NEVER undergoes a cold Power-On Reset (POR).

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
   - Result: COMPLETE ENUMERATION FAILURE on target silicon (detailed in Part 3).

================================================================================
PART 3: FORENSIC FAILURE ANALYSIS OF LATEST SILICON BOOT (Commit d258953652c4)
================================================================================
Below is the verbatim minicom bootlog from the target hardware running Linux 7.1.0 with commit d258953652c4:

```text
[    1.179068] sun60i-a733-usb2-phy 6b00000.phy: Allwinner A733 USB 2.0 PHY registered at [mem 0x06b00000-0x06b007ff flags 0x200] (tune=0x143338d6)
[    1.179402] dwc3 6a00000.usb: DWC3 core probe: GSNPSID raw = 0x33313130 (IP=3331)
[    1.284413] xhci-hcd xhci-hcd.0.auto: xHCI Host Controller
[    1.284426] xhci-hcd xhci-hcd.0.auto: new USB bus registered, assigned bus number 1
[    1.284828] xhci-hcd xhci-hcd.0.auto: hcc params 0x0118ffc5 hci version 0x120 quirks 0x0000808000000010
[    1.284998] xhci-hcd xhci-hcd.0.auto: irq 421, io mem 0x06a00000
[    1.285089] xhci-hcd xhci-hcd.0.auto: xHCI Host Controller
[    1.285094] xhci-hcd xhci-hcd.0.auto: new USB bus registered, assigned bus number 2
[    1.285100] xhci-hcd xhci-hcd.0.auto: Host supports USB 3.1 Enhanced SuperSpeed
[    1.285549] hub 1-0:1.0: USB hub found
[    1.285562] hub 1-0:1.0: 1 port detected
[    1.285721] usb usb2: We don't know the algorithms for LPM for this host, disabling LPM.
[    1.286054] hub 2-0:1.0: USB hub found
[    1.286069] hub 2-0:1.0: 1 port detected
...
[    1.524999] usb 1-1: new high-speed USB device number 2 using xhci-hcd
... (1.8-second gap with no prints) ...
[    3.317008] usb 1-1: new full-speed USB device number 3 using xhci-hcd
[    3.430095] usb 1-1: device descriptor read/64, error -71
[    3.646066] usb 1-1: device descriptor read/64, error -71
[    3.861007] usb 1-1: new full-speed USB device number 4 using xhci-hcd
[    3.984254] usb 1-1: device descriptor read/all, error -71
[    3.984493] usb usb1-port1: attempt power cycle
[    4.360022] usb 1-1: new full-speed USB device number 5 using xhci-hcd
[    4.360105] usb 1-1: Device not responding to setup address.
[    4.565064] usb 1-1: Device not responding to setup address.
[    4.773011] usb 1-1: device not accepting address 5, error -71
[    4.773152] usb 1-1: WARN: invalid context state for evaluate context command.
[    4.885005] usb 1-1: new full-speed USB device number 6 using xhci-hcd
[    4.896248] usb 1-1: not running at top speed; connect to a high speed hub
[    4.896484] usb 1-1: unable to read config index 0 descriptor/start: -71
[    4.896494] usb 1-1: can't read configurations, error -71
[    4.896671] usb usb1-port1: unable to enumerate USB device
# lsusb
Bus 005 Device 001: ID 1d6b:0001 Linux 7.1.0 ohci_hcd Generic Platform OHCI controller
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
3. During `hub_port_reset()`, the hub fails the High-Speed handshake (Chirp K / Chirp J) and reports Full-Speed.
4. Because `oldspeed (USB_SPEED_HIGH) != udev->speed (USB_SPEED_FULL)`, `hub_port_init` exits silently to `fail` without printing an error (because `dev_dbg` is disabled).
5. At 3.317s, the hub retry logic reallocates the device as Full-Speed Device 3. In Full-Speed mode on this DWC3 host, all subsequent descriptor reads time out or fail with protocol error `-71`.

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

================================================================================
PART 5: COMPLETE VERBATIM CODE & VENDOR REFERENCE ATTACHMENTS
================================================================================

--- FILE: drivers/phy/allwinner/phy-sun60i-usb2.c (Mainline Linux 7.1) ---
```c
// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner A733 (sun60iw2) USB 2.0 PHY driver for DWC3
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define SUN60I_DEFAULT_PHY_TUNE	0x143338d6

struct sun60i_usb2_phy {
	void __iomem *base;
	struct regulator *vbus;
	u32 tune_param;
};

#define PHY_USB2_ISCR		0x00
#define PHY_USB2_PHYCTL		0x10
#define PHY_USB2_PHYTUNE	0x18
#define SERDES_TOP_SUBSYS_BGR	0x06c00008

#define PHYCTL_VBUSVLDEXT	BIT(5)
#define PHYCTL_SIDDQ		BIT(3)
#define PHYCTL_COMMONONN	BIT(2)
#define PHYCTL_RESET		BIT(0)

static void sun60i_usb2_phy_hw_init(struct sun60i_usb2_phy *priv)
{
	void __iomem *subsys_bgr;
	void __iomem *syscfg;
	u32 val;

	/* 1. SerDes Top Bridge: ACLK/HCLK clock and reset deassertion */
	subsys_bgr = ioremap(SERDES_TOP_SUBSYS_BGR, 4);
	if (subsys_bgr) {
		val = readl(subsys_bgr);
		val |= BIT(17) | BIT(16) | BIT(4); /* ACLK_EN, HCLK_EN, USB2P0_PHY_RSTN */
		val &= ~BIT(21); /* Clear USB3P1_ONLY_UTMI_CLK_SEL: preserve 60MHz internal PLL */
		writel(val, subsys_bgr);
		iounmap(subsys_bgr);
	}

	/* 2. SYSCFG: Resistor Auto-Calibration (200-ohm target) */
	syscfg = ioremap(0x03000160, 0x10);
	if (syscfg) {
		/* Set target trim to 0xc8 (200 ohm decimal) */
		val = readl(syscfg + 0x08);
		val &= ~GENMASK(15, 8);
		val |= (0xc8 << 8);
		writel(val, syscfg + 0x08);

		/* Enable PCIE_USB 200 ohm trim and trigger auto-calibration */
		val = readl(syscfg + 0x00);
		val |= BIT(10) | BIT(0);
		writel(val, syscfg + 0x00);
		iounmap(syscfg);
	}

	/* Wait for analog bias and calibration currents to stabilize */
	usleep_range(200, 500);

	/* 3. ISCR: Force VBUS valid and ID low to lock Host mode */
	writel(0x0000b000, priv->base + PHY_USB2_ISCR);

	/* 4. Apply Analog Tuning Parameters before releasing reset */
	writel(priv->tune_param, priv->base + PHY_USB2_PHYTUNE);

	/* 5. PHYCTL: Assert analog macro reset, power on transceiver, clear SIDDQ */
	val = readl(priv->base + PHY_USB2_PHYCTL);
	val |= 0x000e2434;
	val &= ~PHYCTL_SIDDQ;
	val |= PHYCTL_RESET;
	writel(val, priv->base + PHY_USB2_PHYCTL);

	udelay(20);

	/* Deassert macro reset */
	val &= ~PHYCTL_RESET;
	writel(val, priv->base + PHY_USB2_PHYCTL);

	/* Allow 60 MHz UTMI clock and PLL lock */
	usleep_range(1500, 2000);
}

static int sun60i_usb2_phy_init(struct phy *phy)
{
	struct sun60i_usb2_phy *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->vbus) {
		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;
		/* Let 20uF downstream rail charge and FE1.1S RC delay clear */
		msleep(50);
	}

	sun60i_usb2_phy_hw_init(priv);
	return 0;
}

static int sun60i_usb2_phy_exit(struct phy *phy)
{
	struct sun60i_usb2_phy *priv = phy_get_drvdata(phy);
	u32 val;

	val = readl(priv->base + PHY_USB2_PHYCTL);
	val &= ~(BIT(10) | BIT(5));
	val |= PHYCTL_SIDDQ;
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

	/* Initialize hardware registers and ungate SerDes bus bridge */
	sun60i_usb2_phy_hw_init(priv);

	dev_info(dev, "Allwinner A733 USB 2.0 PHY registered at %pr (tune=0x%08x)\n",
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
		regulator-always-on;
		regulator-boot-on;
		off-on-delay-us = <200000>; /* 200ms to bleed 20uF capacitor bank */
		startup-delay-us = <100000>; /* 100ms for FE1.1S 12MHz crystal to settle */
		status = "okay";
	};

	u2phy: phy@6b00000 {
		compatible = "allwinner,sun60i-a733-usb2-phy";
		reg = <0x06b00000 0x800>;
		power-domains = <&pck600 PD_USB2>;
		vbus-supply = <&reg_usb1_vbus>;
		aw,phy_tune_param = <0x143338d6>;
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
		snps,parkmode_disable_hs_quirk;
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

1. High-Speed Drop During `hub_port_reset()`:
   - Why did the connection initially detect as High-Speed (`usb 1-1: new high-speed USB device number 2`) at 1.524s, but then during `hub_port_reset()` fail to complete Chirp K/J and drop to Full-Speed (`oldspeed != udev->speed`)?
   - What specific electrical or register mechanism triggers this drop? Is it incorrect line termination impedance from SYSCFG auto-calibration, corrupted squelch threshold in `PHYTUNE`, or a missing transceiver settling delay?

2. Resistor Calibration (SYSCFG 0x03000160 / 0x03000168):
   - Contrast our mainline code with vendor `phy_rescal_set_v2()`.
   - What is the exact sequence to calibrate the 45-ohm HS termination resistors on the A733 SoC without leaving `CAL_EN` asserted or overwriting the factory eFuse trim with a static 0xC8?

3. VBUS Power Sequencing vs. FE1.1S Reset:
   - Given that the FE1.1S hub core 3.3V rail is permanently active from DCDC1, what is the exact Linux regulator sequence needed to force a clean Power-On Reset through VBUSM without triggering brownout latch-up?
   - Should `phy-sun60i-usb2.c` execute `regulator_enable -> regulator_disable -> regulator_enable` with `off-on-delay-us = 200000`, or does the regulator core handle this when `regulator-always-on` is removed?

4. Single-Variable Patch Proposal:
   - Provide the EXACT, minimal diff to `drivers/phy/allwinner/phy-sun60i-usb2.c` and `sun60i-a733-cubie-a7a.dts` that restores the proven baseline and resolves the High-Speed port reset drop.
   - Explain the exact physics and kernel driver mechanics of every changed line.
```
