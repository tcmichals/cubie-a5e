# 🛸 Platform Guide: Radxa Cubie A7Z (Allwinner A733 Compact Form Factor)

This document details the hardware architecture, pinout, schematic analysis, and software bring-up specifications for the **Radxa Cubie A7Z** — the ultra-compact Raspberry Pi Zero-sized flight controller and embedded AI board based on the **Allwinner A733 SoC**.

---

## 1. Board Overview & Physical Specifications

Target revision: **v2.00** (owner, 2026-11-13). Specification source: Radxa product page
<https://docs.radxa.com/en/cubie/a7z?a7z-revision=v2> (read 2026-11-13). Rows marked *unverified*
are not on that page and still need the v2.00 schematic or a silicon readback.

| Parameter | Radxa Cubie A7Z Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | **Allwinner A733 (`sun60iw2p1`)** | 2× Cortex-A76 (Big @ 2.0 GHz) + 6× Cortex-A55 (LITTLE @ 1.8 GHz) |
| **Form Factor** | **Ultra-Compact Zero (65 mm × 30 mm)** | Space and weight optimized for micro-drones and compact gimbals |
| **RAM** | **LPDDR4/4x (1 / 2 / 4 / 8 / 16 GiB)** | Radxa page. Auto-trained by `boot0` |
| **NPU AI Core** | **3.0 TOPs (INT8)** / **1.5 TOPs (FP16)** | VeriSilicon Vivante VIP9000 at MMIO `0x03600000` (IRQ 65) |
| **Video Engine (VPU)** | **Encode H.264/H.265 4K@30; decode H.265/VP9/AVS2 4K@60, H.264 4K@30** | Two VE instances: decoder `0x01C0E000` (SPI 49), encoder `0x01C10000` (SPI 48, PD 3); see `docs/common/A733_VE_H264_ENCODER_PLAN.md` |
| **Camera Interface** | **2× MIPI CSI-2, 4-lane each (v2.00)** | v1.10/v1.11 has 1× 4-lane. Connector type and pinout not on the Radxa page: check the v2.00 schematic |
| **Wireless Interface** | **Wi-Fi 6 + Bluetooth 5.4**, external antenna connector | Radxa page gives no chip vendor; AIC8800 on SDIO MMC2 (`0x04022000`) *unverified* for v2.00 |
| **Display Output** | **Micro-HDMI (4K@60), USB-C DisplayPort Alt Mode, 4-lane MIPI DSI (v2.00 only)** | Controller at `0x05520000` |
| **Storage** | **microSD; optional onboard UFS (up to 1 TB); PCIe 3.0 x1 FPC (NVMe)** | Radxa page. Mainline A733 PCIe and UFS support: not checked |
| **USB** | 1× USB-C 2.0 OTG (power + data), 1× USB-C 3.0 OTG with DP Alt Mode | Radxa page |
| **Fan** | 3-pin connector, 0.8 mm pitch | Radxa page; heat sink available |
| **Operating temperature** | 0 °C to 50 °C (recommended) | Radxa page |
| **Power Management** | **X-Powers AXP318 PMIC** *(unverified)* | Not on the Radxa page. Claimed on `r_i2c0` at `0x07083000` (PL0/PL1): if true, Linux owns that bus and the E902 must not use it for sensors |
| **Co-Processor** | **XuanTie E902 RISC-V (RV32EMC)** | Low-latency flight control loop & telemetry engine |
| **Reference Schematic**| `vendor-a733-reference/schematics/radxa_cubie_a7z_schematic_v1.11.pdf` | **v1.11, not v2.00**, and not in this repo on this PC. Get the v2.00 schematic |

---

## 2. Hardware Architecture & Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Radxa Cubie A7Z (Zero Form Factor)                 │
│                                                                             │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                      Allwinner A733 Octa-Core SoC                    │  │
│   │   • 2x Cortex-A76 (2.0 GHz) + 6x Cortex-A55 (1.8 GHz) DynamIQ        │  │
│   │   • 3.0 TOPs VeriSilicon VIP9000 NPU (0x03600000)                    │  │
│   │   • VE decoder (0x01C0E000) + VE H.264/H.265 encoder (0x01C10000)    │  │
│   │   • XuanTie E902 Real-Time Co-Processor (CFG 0x07032000)             │  │
│   │   • PowerVR BXM-4-64 MC1 GPU                                         │  │
│   └───────────────────┬───────────────────────────┬──────────────────────┘  │
│                       │                           │                         │
│       ┌───────────────┴───────────────┐   ┌───────┴─────────────────┐       │
│       │ Wi-Fi 6 / BT 5.4              │   │ 2× MIPI CSI-2 4-lane    │       │
│       │ (SDIO MMC2 @ 0x04022000)      │   │ (v2.00; IMX415 etc.)    │       │
│       └───────────────────────────────┘   └─────────────────────────┘       │
│                       │                           │                         │
│       ┌───────────────┴───────────────┐   ┌───────┴─────────────────┐       │
│       │ Micro-HDMI 2.0 (4K@60)        │   │ Dual USB Type-C (OTG/Host)│     │
│       │ (0x05520000)                  │   │ (0x04101000 / 0x04200000)│      │
│       └───────────────────────────────┘   └─────────────────────────┘       │
│                       │                           │                         │
│       ┌───────────────┴───────────────┐   ┌───────┴─────────────────┐       │
│       │ MicroSD Slot (MMC0)           │   │ 40-Pin Expansion Header │       │
│       │ (0x04020000)                  │   │ (UART / I2C / SPI / PWM)│       │
│       └───────────────────────────────┘   └─────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Peripheral Comparison: Cubie A7A vs. Cubie A7Z

| Hardware Subsystem | Radxa Cubie A7A (Full SBC) | Radxa Cubie A7Z (Zero Form Factor) | Mainline Compatibility |
| :--- | :--- | :--- | :--- |
| **Physical Dimensions**| Standard SBC (85 mm × 56 mm) | **Ultra-Compact (65 mm × 30 mm)** | Shared silicon drivers |
| **Ethernet** | Dual Gigabit RJ45 (`GMAC0`/`GMAC1`) | **None (Wi-Fi 6 / USB Ethernet only)** | A7Z omits PHY chip to save weight |
| **Video Out** | Full-Size HDMI 2.0 | **Micro-HDMI 2.0** | Same `0x05520000` controller |
| **USB Configuration** | 2× Type-A Host + 1× Type-C OTG | **2× Type-C (1× Power/OTG + 1× Host)**| Shared EHCI/OHCI/DWC3 nodes |
| **Wi-Fi / BT** | AIC8800 (SDIO / USB) | **AIC8800 (High-Speed SDIO)** | Shared `aic8800_fdrv.ko` |
| **Camera Port** | 31-pin J7 (`CAM_31P`), MCSIB 4-lane | **2× 4-lane MIPI CSI-2 (v2.00)** | Shared `sunxi-csi` & ISP |
| **Storage** | microSD (+ others, see A7A docs) | **microSD, optional UFS, PCIe 3.0 x1 FPC** | PCIe/UFS mainline: not checked |
| **NPU AI Engine** | 3.0 TOPs @ `0x03600000` | **3.0 TOPs @ `0x03600000`** | 100% Identical |
| **VPU Video Engine** | Decoder `0x01C0E000`, encoder `0x01C10000` | **Same** | Same silicon |
| **Power Co-Processor** | XuanTie E902 (`scp.fex` U-Boot PM) | **XuanTie E902 (`scp.fex` U-Boot PM)** | 100% Identical |

---

## 4. Software Bring-Up & Mainline Architecture Sharing

Because the **Cubie A7Z** uses the exact same Allwinner A733 silicon as the **Cubie A7A**, 100% of our bring-up work applies directly:
1. **Boot Chain**: Same 2-stage hybrid boot (`boot0` LPDDR4/4x PHY training on the A7Z + Mainline TF-A BL31 + U-Boot 2026.01).
2. **Clock Tree & Base Registers**: Shared Main CCU (`0x02002000`), PRCM R_CCU (`0x07010000`), PRCM R_PIO (`0x07025000`), PMIC I2C (`0x07083000`).
3. **Interrupt Table**: Identical GICv3 interrupt map (GIC-600 @ `0x03400000`/`0x03460000`).
4. **Device Tree**: `sun60i-a733-cubie-a7z.dts` simply disables the `gmac0`/`gmac1` nodes and routes USB over Type-C ports.
