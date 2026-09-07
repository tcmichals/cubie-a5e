# Allwinner A733 / Radxa Cubie A7A USB & Peripheral Subsystem Reference

**Board Target:** Radxa Cubie A7A (SoC: Allwinner A733 / `sun60iw2`, Octa-Core 2×A76 + 6×A55)  
**Kernel Baseline:** Upstream Mainline Linux 7.1 (`PREEMPT_RT`)

---

## 1. Physical Hardware & Schematic Topology (Radxa Cubie A7A V1.10)

```mermaid
graph TD
    subgraph SoC["Allwinner A733 SoC"]
        DWC3["USB2 / DWC3 Controller<br/>Base: 0x06A00000<br/>PHY: 0x06B00000 (Tuning 0x143338D6)"]
        EHCI1["USB1 Host Controller<br/>Base: 0x04200000 (EHCI1 / OHCI1)<br/>PHY: phy@4100400 (PHY 1)"]
        EHCI0["USB0 Host Controller<br/>Base: 0x04101000 (EHCI0 / OHCI0)<br/>PHY: phy@4100400 (PHY 0)"]
        PM5["GPIO PM5<br/>Net: USB_HOST_EN"]
        PL2["GPIO PL2<br/>Net: USB0-DRVVBUS"]
        PM0["GPIO PM0<br/>Net: USB_WIFI_PWR"]
        PM1["GPIO PM1<br/>Net: WL-REG-ON"]
    end

    subgraph PowerRegulators["Power Switches & PMIC"]
        U5["U5 Power Switch (SGM2576)<br/>Feeds Hub VBUS (VCC5V0_USB20)"]
        U2["U2 Power Switch (SGM2576)<br/>Feeds Type-A 5V (VCC5V0_USB30_OTG)"]
        Q8["Q8 P-MOSFET Switch<br/>Feeds Wi-Fi 3.3V (WIFI_3V3)"]
    end

    subgraph Peripherals["Physical Ports & Onboard Chips"]
        U6["U6: FE1.1S 4-Port USB 2.0 Hub<br/>Reset: Analog RC delay (R60/C155, ~1ms)<br/>No SoC Reset GPIO!"]
        U3["U3: AIC8800 Wi-Fi 6 Module<br/>Physically connected to Hub Port 4!"]
        PortBottom["Bottom Type-A USB 2.0 Pins"]
        PortTypeC["Type-C Connector J16"]
    end

    PM5 -->|Active High| U5
    U5 -->|5V VBUS| U6
    DWC3 -->|USB2-DP / USB2-DM| U6
    U6 -->|Port 4 (USB4_DP/DM)| U3

    PM0 -->|Active High| Q8
    Q8 -->|3.3V Power| U3
    PM1 -->|Chip Enable| U3

    PL2 -->|Active High| U2
    U2 -->|5V VBUS| PortBottom
    EHCI1 -->|USB1-DP / USB1-DM| PortBottom

    EHCI0 -->|USB0-DP / USB0-DM| PortTypeC
```

---

## 2. The 3 Hardware USB Paths

### Path 1: USB2 / DWC3 Controller (`0x06A00000`) -> FE1.1S Hub (`U6`) -> AIC8800 (`U3`)
- **Controller**: Synopsys DWC3 USB controller paired with Allwinner H6/A733 glue (`allwinner,sun50i-h6-dwc3`).
- **Power Domain**: `power-domains = <&pck600 PD_USB2>;` (Domain 8 in `sun55i-pck600.c`).
- **Downstream Hub Power**: Power switch `U5` (SGM2576) is gated by **`PM5` (`USB_HOST_EN`)**. Must be `regulator-always-on; regulator-boot-on;` in DT.
- **FE1.1S Reset**: Pin 16 `XRSTJ` is strictly an RC circuit (`R60` 10k to 3.3V, `C155` 100nF to GND). It has **NO SoC GPIO reset line**.
- **AIC8800 Wi-Fi 6 (`U3`)**:
  - Connected via **USB 2.0** on Hub downstream Port 4 (`USB4_DP` / `USB4_DM`), **NOT SDIO** and **NOT PCIe**.
  - Power: Switched 3.3V from `PM0` (`USB_WIFI_PWR`).
  - Enable: Direct 3.3V chip enable from `PM1` (`WL-REG-ON`).
  - Kernel package: `BR2_PACKAGE_AIC8800_DRIVER_USB=y`.

### Path 2: USB1 Host Controller (`0x04200000`) -> Bottom Type-A Port USB 2.0 Lines
- **Controller**: `allwinner,sun50i-h616-ehci` / `ohci` + `usbphy 1`.
- **VBUS Power**: Switched by `U2` (SGM2576), gated by **`PL2` (`USB0-DRVVBUS`)**.
- **CCU Clocks**: Register `0x130C` requires gating both OHCI (bit 0) and EHCI DMA engine (bit 4): `BIT(4) | BIT(0)`.

### Path 3: USB0 Host Controller (`0x04101000`) -> Type-C Port J16
- **Controller**: `allwinner,sun50i-h616-ehci` / `ohci` + `usbphy 0`.
- **PHY Configuration**: `usbphy: phy@4100400` requires explicit `dr_mode = "host";`. Without this or an ID-detect pin, `phy-sun4i-usb.c` defaults to peripheral mode, disables PHY passby, and reroutes away from EHCI/OHCI to a dummy MUSB core.

---

## 3. Kernel Configuration (`.config`) Critical Matrix

| Config Symbol | Required Value | Purpose |
| :--- | :--- | :--- |
| `CONFIG_SUN55I_PCK600` | `=y` (Built-in) | **Mandatory**. PCK-600 power domain driver. If `=m` or missing, `usb_dwc3` probe defers forever (`-EPROBE_DEFER`). |
| `CONFIG_USB_DWC3_OF_SIMPLE` | `=y` (Built-in) | **Mandatory**. Matches `allwinner,sun50i-h6-dwc3` wrapper to spawn `snps,dwc3` core. |
| `CONFIG_USB_DWC3_HOST` | `=y` | Forces DWC3 host mode operation. |
| `CONFIG_PHY_SUN4I_USB` | `=y` | Drives `usbphy: phy@4100400` (PHY0 and PHY1). |
| `CONFIG_USB_EHCI_HCD` & `CONFIG_USB_EHCI_PLATFORM` | `=y` | Host 0 and Host 1 USB 2.0 high-speed controller. |
| `CONFIG_USB_OHCI_HCD` & `CONFIG_USB_OHCI_PLATFORM` | `=y` | Host 0 and Host 1 USB 1.1 full/low-speed companion controller. |
| `CONFIG_AIC8800_USB_SUPPORT` | `=m` or Buildroot package | Kernel module for AIC8800 Wi-Fi 6 on hub port 4. |

---

## 4. Build Speed Optimization (Trimming `.config`)

- Upstream ARM64 `defconfig` enables >4,700 configurations (>3,300 built-in), compiling drivers for dozens of third-party SoCs (Qualcomm, Rockchip, Apple, NXP, Broadcom, MediaTek, Tegra, Cavium, etc.), taking 15–25 minutes.
- By switching Buildroot to `BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y` and providing a standalone, sunxi-focused `.config`, compile times drop to **~1–2 minutes** while keeping PREEMPT_RT, UIO, eMMC, Ethernet, AIC8800, and USB fully operational.
