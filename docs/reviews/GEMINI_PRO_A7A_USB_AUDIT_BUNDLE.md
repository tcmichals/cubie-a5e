# Radxa Cubie A7A USB 2.0 / DWC3 / FE1.1S Hub Complete System Audit Bundle
# Target: Google Gemini Pro (1.5 Pro / 2.0 Pro)

> **Instructions for Reviewer**:
> Copy and paste this entire document into [Google Gemini Pro](https://gemini.google.com).
> It contains the complete architectural prompt, schematic netlists, device tree nodes, mainline driver code, and vendor reference drivers.

---

```text
You are an elite Linux Kernel USB Subsystem Maintainer and hardware electrical engineer specializing in Synopsys DesignWare USB3 (DWC3), UTMI+ PHY interfaces, Linux regulator architecture, and Allwinner SoCs.

Perform an adversarial, technically uncompromising system-level audit of our mainline Linux 7.1 USB implementation for the Allwinner A733 SoC on the Radxa Cubie A7A board.

You have the complete hardware schematic netlist, device tree nodes, mainline driver source code, and working vendor BSP reference drivers provided below in this document.

================================================================================
PART 1: HARDWARE & SCHEMATIC GROUND TRUTH (Radxa Cubie A7A V1.10)
================================================================================
1. Physical USB Topology:
   - Bottom USB Port (CON_U3_U2): Direct point-to-point wiring to SoC balls E36/F36 (USB1-DP/USB1-DM). Driven by SoC EHCI1/OHCI1 (0x04200000) and phy-sun4i-usb. 5V VBUS is switched by U2 (SGM2576) via PL2 (USB0-DRVVBUS).
     STATUS: Always works reliably (completely bypasses DWC3 and the hub).
   - Top USB Port (CON1), Internal Header (J4), and AIC8800 Wi-Fi 6 (U3):
     All wire to downstream ports 1, 2, 3, and 4 of an onboard Genesys Logic FE1.1S USB 2.0 Hub (U6).
   - Upstream FE1.1S Hub (U6):
     Pins DPU (pin 15) and DMU (pin 14) route through 0-ohm resistors R53 and R69 directly to SoC balls C36 and B37 (USB2-DP and USB2-DM).
     STATUS: Driven exclusively by SoC DWC3 (0x06A00000, Synopsys DWC3.1 IP v1.90a) and the Sun60i USB 2.0 Analog PHY (0x06B00000).

2. Power Architecture & Reset Circuits for FE1.1S Hub (U6):
   - Hub Core Power: 3.3V (VCC_3V3_USB20HUB) supplied by DCDC1 via 0-ohm resistor R58.
   - Hardware Reset (XRSTJ, pin 16): There is NO SoC GPIO reset line. Pin 16 is tied to an RC delay network off the 3.3V rail consisting of pull-up resistor R60 (10k 1%) and filter capacitor C155 (100nF 10V). Time constant: tau = 1 ms.
   - 5V VBUS Rail (VCC5V0_USB20): Switched by U5 (SGM2576 power switch).
     - Enable pin EN (pin 4) is driven by SoC GPIO PM5 (USB_HOST_EN).
     - Output VOUT (pin 1) supplies 5V to the downstream USB ports and charges a 20 uF capacitor bank (C151 10uF + C153 10uF).
     - VBUS Monitor (VBUSM, pin 17 of FE1.1S): Driven by a 50% voltage divider from VCC5V0_USB20 formed by R64 (100k 1%) and R65 (100k 1%). Hub detects VBUS valid when VBUSM >= 2.5V.

3. Failure History on Target Silicon:
   - xHCI detected device attach on Port 1 (line status 0x0300B000: 1.5k pull-up detected on D+ by SoC transceiver, VBUS valid).
   - During EP0 GET_DESCRIPTOR, transactions failed repeatedly with:
     "device descriptor read/64, error -71" (-EPROTO / COMP_USB_TRANSACTION_ERROR).
   - The port fell back to Full-Speed and was subsequently disabled.

================================================================================
PART 2: RECENT MAINLINE DRIVER & DEVICE TREE REVISIONS
================================================================================
We implemented the following system-wide updates:

1. Device Tree (sun60i-a733-cubie-a7a.dts):
   - In reg_usb1_vbus: DELETED regulator-always-on and regulator-boot-on.
     Rationale: These properties blocked Linux regulator core from cycling PM5 low. If U-Boot left PM5 high, the hub never saw 0V, preventing a cold reset flush.
   - In reg_usb1_vbus: Set startup-delay-us = <100000> (100 ms).
     Rationale: Forces Linux regulator core to wait 100 ms after asserting PM5 so U5 (SGM2576), the 20 uF capacitor bank, the R64/R65 divider, the XRSTJ RC delay, and the hub's 12 MHz crystal oscillator completely stabilize.
   - In u2phy (phy@6b00000): Added vbus-supply = <&reg_usb1_vbus>;.

2. PHY Driver (drivers/phy/allwinner/phy-sun60i-usb2.c):
   - Added vbus regulator management (devm_regulator_get_optional, regulator_enable in init, regulator_disable in exit).
   - SerDes Top Bridge (0x06C00008 / SERDES_TOP_SUBSYS_BGR):
     Read-modify-write setting ONLY ACLK_EN (bit 17), HCLK_EN (bit 16), and USB2P0_PHY_RSTN (bit 4) without altering Bit 21 or other multiplexer bits, matching vendor combo_usb2_clk_set / combo_usb_clk_set.
   - PHYCTL (0x10): Read-modify-write setting OTGDISABLE (bit 10) and VBUSVLDEXT (bit 5) while clearing SIDDQ (bit 3), preserving wafer analog calibration.
   - SYSCFG (0x03000160/0x168): Configured 200-ohm calibration trim.

3. DWC3 Core (drivers/usb/dwc3/core.c):
   - Host-mode soft reset: In dwc3_core_soft_reset(), when dwc->dr_mode == USB_DR_MODE_HOST, pulses DWC3_GUSB2PHYCFG_PHYSOFTRST and sleeps 50 ms for UTMI 60 MHz phase lock (matching vendor core.c lines 294-318).
   - Dropped the DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK quirk override (strictly device-mode quirk).
   - Retained DWC3_OCFG_SFTRSTMASK in DWC3_OCFG to prevent host controller reset from clearing OTG filters.

================================================================================
PART 3: SOURCE CODE ATTACHMENTS
================================================================================

--- FILE: linux-cubie/drivers/phy/allwinner/phy-sun60i-usb2.c ---
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

	/* Initialize hardware registers */
	sun60i_usb2_phy_hw_init(priv);

	dev_info(dev, "Allwinner A733 USB 2.0 PHY initialized at %pr (tune=0x%08x)\n",
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

--- FILE: Device Tree Excerpts (arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts) ---
```dts
	reg_usb0_vbus: usb0-vbus {
		compatible = "regulator-fixed";
		regulator-name = "usb0-vbus";
		regulator-min-microvolt = <5000000>;
		regulator-max-microvolt = <5000000>;
		gpio = <&r_pio 0 2 GPIO_ACTIVE_HIGH>; /* PL2 */
		gpios = <&r_pio 0 2 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		regulator-always-on;
		regulator-boot-on;
		status = "okay";
	};

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

	/* DWC3 Sun60i USB 2.0 PHY */
	u2phy: phy@6b00000 {
		compatible = "allwinner,sun60i-a733-usb2-phy";
		reg = <0x06b00000 0x800>;
		power-domains = <&pck600 PD_USB2>;
		vbus-supply = <&reg_usb1_vbus>;
		aw,phy_tune_param = <0x143338d6>;
		#phy-cells = <0>;
		status = "okay";
	};

	/* Synopsys DWC3 Controller */
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
		status = "okay";
	};

	/* EHCI1/OHCI1 for bottom USB port */
	ehci1: usb@4200000 {
		compatible = "allwinner,sun60i-a733-ehci", "generic-ehci";
		reg = <0x04200000 0x100>;
		interrupts = <GIC_SPI 149 IRQ_TYPE_LEVEL_HIGH>;
		clocks = <&ccu CLK_BUS_USB1>, <&ccu CLK_USB_OHCI1>;
		resets = <&ccu RST_BUS_USB1>;
		phys = <&usbphy 1>;
		phy-names = "usb";
		status = "okay";
	};
```

--- FILE: drivers/usb/dwc3/core.c Modifications (Diff vs Upstream Linux 7.1) ---
```diff
@@ -329,8 +329,22 @@ int dwc3_core_soft_reset(struct dwc3 *dwc)
 	 * XHCI driver will reset the host block. If dwc3 was configured for
 	 * host-only mode, then we can return early.
 	 */
-	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST)
+	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST ||
+	    dwc->dr_mode == USB_DR_MODE_HOST) {
+		u32 usb2_port;
+
+		usb2_port = dwc3_readl(dwc, DWC3_GUSB2PHYCFG(0));
+		usb2_port |= DWC3_GUSB2PHYCFG_PHYSOFTRST;
+		dwc3_writel(dwc, DWC3_GUSB2PHYCFG(0), usb2_port);
+
+		usleep_range(1000, 2000);
+
+		usb2_port &= ~DWC3_GUSB2PHYCFG_PHYSOFTRST;
+		dwc3_writel(dwc, DWC3_GUSB2PHYCFG(0), usb2_port);
+
+		msleep(50);
 		return 0;
+	}
 
 	reg = dwc3_readl(dwc, DWC3_DCTL);
 	reg |= DWC3_DCTL_CSFTRST;
@@ -1519,6 +1533,16 @@ int dwc3_core_init(struct dwc3 *dwc)
 		dwc3_writel(dwc, DWC3_GUCTL1, reg);
+
+		/*
+		 * Prevent host/device reset from resetting OTG core.
+		 * If we don't do this then xhci_reset (USBCMD.HCRST) will reset
+		 * the signal outputs sent to the PHY, the OTG FSM logic of the
+		 * core and also the resets to the VBUS filters inside the core.
+		 */
+		reg = dwc3_readl(dwc, DWC3_OCFG);
+		reg |= DWC3_OCFG_SFTRSTMASK;
+		dwc3_writel(dwc, DWC3_OCFG, reg);
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

--- FILE: Vendor Reference: A7A_kernel/linux-a733/bsp/drivers/usb/dwc3/phy-sunxi-plat.c ---
```c
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
```

================================================================================
PART 4: TARGET AUDIT QUESTIONS
================================================================================
1. End-to-End VBUS Sequencing & Power-Cycle Guarantee:
   - Does removing `regulator-always-on` and `regulator-boot-on` from `reg_usb1_vbus`, combined with binding `vbus-supply = <&reg_usb1_vbus>` to `u2phy`, guarantee that Linux will drop `PM5` low at boot and enforce the 100 ms `startup-delay-us` when `u2phy` powers on?
   - Is a 100 ms startup delay sufficient for the SGM2576 power switch (charging C151/C153 20uF) and the FE1.1S hub's external RC delay on XRSTJ (R60=10k, C155=100nF) to completely leave reset and stabilize its 12 MHz crystal?
2. UTMI Clock Phase Synchronization:
   - Does our `PHYSOFTRST` pulse with 50 ms delay in `dwc3_core_soft_reset()` properly establish UTMI clock phase lock with the CCU 60 MHz generator (CCU 0x1360 = 0x81000004)?
3. Analog Calibration & PHYCTL Integrity:
   - Does our read-modify-write on `PHY_USB2_PHYCTL` (`0x10`) and SYSCFG 200-ohm calibration (`0x03000160`/`0x168`) faithfully preserve all factory silicon calibration flags?
4. Are There ANY Remaining Traps?
   - Inspect the entire system: device tree, PHY driver, DWC3 core, and hardware schematic. Are there any hidden race conditions, unclocked domains, or voltage dependencies that could still cause `device descriptor read/64, error -71`?

Give an authoritative, unvarnished verdict.
```
