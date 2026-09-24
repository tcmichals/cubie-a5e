# Allwinner A733 (sun60i) DWC3 USB 2.0 PHY & FE1.1S Hub Engineering Guide

**Target Subsystems**: `linux-sunxi`, `linux-usb`, `drivers/phy/allwinner`, `regulator`  
**Platform**: Radxa Cubie A7A (Allwinner A733 / sun60iw2p1)  
**Status**: Upstream RFC Integration Guide  

---

## 1. Executive Summary & Root Cause Analysis

Under mainline Linux 7.1, the Allwinner A733 Synopsys DWC3 USB 2.0 controller on the Radxa Cubie A7A failed to enumerate downstream devices connected to the onboard Genesys Logic FE1.1S USB 2.0 hub (affecting the top USB Type-A port, the internal USB header, and the onboard AIC8800 Wi-Fi 6 module).

Transactions repeatedly stalled during control transfers on Endpoint 0:
```text
usb 1-1: new high-speed USB device number 2 using xhci-hcd
usb 1-1: device descriptor read/64, error -71
usb 1-1: device descriptor read/64, error -71
usb 1-1: device not accepting address 2, error -71
usb usb1-port1: unable to enumerate USB device
```

Comprehensive schematic tracing, register auditing, and vendor BSP cross-referencing identified **three concurrent root causes**:

1. **Regulator Framework State Trap & Missing Capacitor Bleed-Off**:
   - The hub's reset pin (`XRSTJ`) is tied to the permanent 3.3V board rail (`DCDC1`), so it is never reset by toggling power during warm reboots.
   - The hub's reset state machine is triggered **exclusively via its VBUS Monitor input (`VBUSM`, pin 17)**, which samples the switched 5V VBUS rail (`VCC5V0_USB20`) via a $200\,\text{k}\Omega$ voltage divider ($R_{64} = 100\,\text{k}\Omega, R_{65} = 100\,\text{k}\Omega$).
   - The Device Tree originally declared `reg_usb1_vbus` with `regulator-always-on; regulator-boot-on;`. This explicitly forbade the Linux regulator core from driving `PM5` (`USB_HOST_EN`) low.
   - If U-Boot left `PM5` high, `VCC5V0_USB20` never saw $0\text{ V}$. Furthermore, the $20\,\mu\text{F}$ capacitor bank ($C_{151} + C_{153}$) sustained charge above the FE1.1S brown-out detection threshold ($\sim 2.7\text{ V}$), locking the hub's internal Serial Interface Engine (SIE) into an indeterminate state inherited from the bootloader.

2. **Analog Wafer Trim Overwrite**:
   - The initial mainline PHY driver overwrote `PHY_USB2_PHYCTL` (`0x10`) with a raw literal (`0x000e2434`). This destroyed factory wafer calibration flags (such as `VATESTENB`, bits [1:0]) initialized by eFuse/BROM, degrading transmitter eye margins.

3. **UTMI Clock Domain Phase Misalignment**:
   - The DWC3 IP core operates on an external 60 MHz UTMI clock (`CLK_USB2_U2_REF` / CCU `0x1360`). Following PHY initialization or clock changes, the Synopsys DWC3 UTMI elastic FIFO requires a soft reset pulse (`DWC3_GUSB2PHYCFG_PHYSOFTRST`) to phase-align internal clock domains. Without this pulse in host mode, UTMI packet transactions corrupted with `EPROTO` (-71).

---

## 2. Hardware Schematic Ground Truth (Radxa Cubie A7A V1.10)

```
                            ┌────────────────────────────────────────────────────────┐
                            │                  Allwinner A733 SoC                    │
                            │                                                        │
                            │   Synopsys DWC3 (0x06A00000)                           │
                            │        │                                               │
                            │      Sun60i USB 2.0 PHY (0x06B00000)                   │
                            │        │  (USB2-DP / USB2-DM)                          │
                            │        │  (SoC balls C36 / B37)                        │
                            └────────┼───────────────────────────────────────────────┘
                                     │
                                     │ Upstream DPU/DMU (via 0R resistors R53/R69)
                                     ▼
                    ┌─────────────────────────────────┐
                    │  Genesys Logic FE1.1S Hub (U6)  │
                    │                                 │
   DCDC1 (3.3V) ───►│ VDD (pin 5)                     │
  (via 0R R58)      │ XRSTJ (pin 16, 10k/100nF RC)    │
                    │                                 │
 PM5 (USB_HOST_EN) ─►[U5: SGM2576] ──► VBUSM (pin 17) │ (via R64/R65 100k/100k divider)
                    │   VOUT (5V)     │               │
                    │   +20uF bank    │               │
                    └────────┬────────┴───────────────┘
                             │
            ┌────────────────┼────────────────┐
            ▼                ▼                ▼
     Downstream Port 1  Downstream Port 2  Downstream Port 4
     Top USB 2.0 Port   Internal Header    AIC8800 Wi-Fi 6
     (CON1)             (J4)               Module (U3)
```

### Component Details
* **Power Switch `U5`**: SGMICRO SGM2576 single-channel power switch.
  * Enable Pin `EN` (pin 4): Driven by SoC `PM5` (`USB_HOST_EN`). Active-high.
  * **Automatic Output Discharge**: Features an integrated $\sim 100\,\Omega$ discharge N-MOSFET on `VOUT` when `EN` is driven low.
* **Capacitor Bank**: $C_{151}$ ($10\,\mu\text{F}$) + $C_{153}$ ($10\,\mu\text{F}$) = $20\,\mu\text{F}$ directly on `VCC5V0_USB20`.
* **Discharge Duration**:
  * SGM2576 internal FET: $\tau = 100\,\Omega \times 20\,\mu\text{F} = 2.0\text{ ms}$ (drained to $< 10\text{ mV}$ in $\sim 10\text{ ms}$).
  * Secondary divider bleed: $R_{64} + R_{65} = 200\,\text{k}\Omega$.
  * Device Tree specification `off-on-delay-us = <200000>` (200 ms) guarantees a full bleed-off margin regardless of component tolerances or board revisions.

---

## 3. Mainline Upstream Architecture & Clean Implementation

### 3.1 Device Tree Specification (`sun60i-a733-cubie-a7a.dts`)

Hardware electrical timing characteristics are strictly decoupled from C drivers and defined in Device Tree:

```dts
	reg_usb1_vbus: usb1-vbus {
		compatible = "regulator-fixed";
		regulator-name = "usb1-vbus";
		regulator-min-microvolt = <5000000>;
		regulator-max-microvolt = <5000000>;
		gpios = <&r_pio 1 5 GPIO_ACTIVE_HIGH>; /* PM5 (USB_HOST_EN) */
		enable-active-high;

		/* Hardware RC Timing Properties */
		off-on-delay-us = <200000>; /* 200 ms: Bleed 20uF bank below FE1.1S BOR */
		startup-delay-us = <100000>; /* 100 ms: SGM2576 soft-start & 12 MHz crystal lock */

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
```

### 3.2 PHY Driver Implementation (`drivers/phy/allwinner/phy-sun60i-usb2.c`)

The driver adheres strictly to the Linux regulator subsystem consumer model:

```c
static int sun60i_usb2_phy_init(struct phy *phy)
{
	struct sun60i_usb2_phy *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->vbus) {
		/*
		 * 1. Increment consumer use_count from 0 to 1.
		 * Synchronizes software tracking with bootloader hardware state.
		 */
		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;

		/*
		 * 2. Force power cycle to flush the downstream hub state machine.
		 * Because use_count is now 1, this drops use_count to 0 cleanly,
		 * pulls PM5 low, and timestamps rdev->last_off without triggering
		 * kernel warnings about unbalanced disables.
		 */
		regulator_disable(priv->vbus);

		/*
		 * 3. Turn it back on. The Linux regulator core automatically
		 * checks rdev->last_off and sleeps for `off-on-delay-us` (200ms)
		 * via fsleep() before asserting PM5, then sleeps for
		 * `startup-delay-us` (100ms) before returning.
		 */
		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;
	}

	sun60i_usb2_phy_hw_init(priv);
	return 0;
}
```

### 3.3 Analog Silicon Calibration Preservation

To prevent wiping out factory eFuse wafer calibration:

```c
static void sun60i_usb2_phy_hw_init(struct sun60i_usb2_phy *priv)
{
	void __iomem *subsys_bgr;
	u32 val;

	/*
	 * Enable ACLK/HCLK and deassert PHY reset in SerDes top bridge.
	 * Strictly alters ACLK_EN, HCLK_EN, and USB2P0_PHY_RSTN via read-modify-write
	 * matching vendor combo_usb2_clk_set / combo_usb_clk_set without touching Bit 21.
	 */
	subsys_bgr = ioremap(SERDES_TOP_SUBSYS_BGR, 4);
	if (subsys_bgr) {
		val = readl(subsys_bgr);
		val |= BIT(17) | BIT(16) | BIT(4);
		writel(val, subsys_bgr);
		iounmap(subsys_bgr);
	}

	/* Configure 200-ohm calibration trim in SYSCFG (0x03000000) */
	{
		void __iomem *syscfg = ioremap(0x03000160, 0x10);

		if (syscfg) {
			/* RESCAL_CTRL (0x160): select PCIE_USB 200 ohm trim, clear CAL_EN */
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
```

### 3.4 DWC3 Host-Mode UTMI Soft Reset (`drivers/usb/dwc3/core.c`)

```diff
--- a/drivers/usb/dwc3/core.c
+++ b/drivers/usb/dwc3/core.c
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
```

---

## 4. Upstream Patch Series Structure

For submission to `linux-sunxi@lists.linux.dev` and `linux-usb@vger.kernel.org`:

1. **Patch 1**: `phy: allwinner: sun60i-usb2: add VBUS regulator power-cycle sequencing`
   - Implement `vbus` regulator support.
   - Synchronize consumer use count with bootloader state and trigger cold reset.
   - Preserves wafer analog calibration via read-modify-write.
2. **Patch 2**: `arm64: dts: allwinner: sun60i-a733-cubie-a7a: bind vbus-supply to u2phy with discharge delays`
   - Add `off-on-delay-us = <200000>` and `startup-delay-us = <100000>` to `reg_usb1_vbus`.
   - Remove `regulator-always-on` and `regulator-boot-on`.
   - Attach `vbus-supply = <&reg_usb1_vbus>;` to `u2phy`.
3. **Patch 3**: `usb: dwc3: core: pulse PHYSOFTRST during host-mode core soft reset`
   - Ensures UTMI clock phase lock with external 60 MHz clock generators during host probe.
