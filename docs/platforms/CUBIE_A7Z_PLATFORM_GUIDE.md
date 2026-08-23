# 🛸 Platform Guide: Radxa Cubie A7Z (Allwinner A733 Compact Form Factor)

This document details the hardware architecture, pinout, schematic analysis, and software bring-up specifications for the **Radxa Cubie A7Z** — the ultra-compact Raspberry Pi Zero-sized flight controller and embedded AI board based on the **Allwinner A733 SoC**.

---

## 1. Board Overview & Physical Specifications

| Parameter | Radxa Cubie A7Z Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | **Allwinner A733 (`sun60iw2p1`)** | 2× Cortex-A76 (Big @ 2.0 GHz) + 6× Cortex-A55 (LITTLE @ 1.8 GHz) |
| **Form Factor** | **Ultra-Compact Zero (65 mm × 30 mm)** | Space and weight optimized for micro-drones and compact gimbals |
| **RAM** | **LPDDR5 (2 GiB / 4 GiB / 6 GiB)** | Auto-trained via `boot0` dynamic PHY training |
| **NPU AI Core** | **3.0 TOPs (INT8)** / **1.5 TOPs (FP16)** | VeriSilicon Vivante VIP9000 at MMIO `0x03600000` (IRQ 65) |
| **Video Engine (VPU)** | **4K@30fps H.265 / H.264 Encoder** | Hardware Video Engine at MMIO `0x01C0E000` (IRQ 49) |
| **Camera Interface** | **MIPI CSI-2 (2/4-Lane)** | 15-pin FPC connector (IMX219 8MP, IMX477 12MP HQ, OV5647 5MP) |
| **Wireless Interface** | **AIC8800 Wi-Fi 6 + Bluetooth 5.2** | Connected via High-Speed SDIO on MMC2 (`0x04022000`) |
| **Display Output** | **Micro-HDMI 2.0 (4K@60fps) + MIPI DSI** | Controller at `0x05520000` |
| **Power Management** | **X-Powers AXP318 PMIC** | Connected via `r_i2c0` at `0x07083000` (PL0/PL1) |
| **Co-Processor** | **XuanTie E907 RISC-V (RV32IMAC)** | Low-latency flight control loop & telemetry engine |
| **Reference Schematic**| [`radxa_cubie_a7z_schematic_v1.11.pdf`](file:///home/tcmichals/projects/cubie/vendor-a733-reference/schematics/radxa_cubie_a7z_schematic_v1.11.pdf) | 14-page official hardware schematic |

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
│   │   • 4K@30fps H.265/H.264 Video Engine VPU (0x01C0E000)               │  │
│   │   • XuanTie E907 Real-Time Co-Processor (0x07110000/0x07130000)      │  │
│   │   • PowerVR BXM-4-64 MC1 GPU                                         │  │
│   └───────────────────┬───────────────────────────┬──────────────────────┘  │
│                       │                           │                         │
│       ┌───────────────┴───────────────┐   ┌───────┴─────────────────┐       │
│       │ AIC8800 Wi-Fi 6 / BT 5.2      │   │ MIPI CSI-2 (2/4-Lane)   │       │
│       │ (SDIO MMC2 @ 0x04022000)      │   │ (IMX219 / IMX477 Cam)   │       │
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
| **Camera Port** | 15-pin / 22-pin FPC MIPI CSI-2 | **15-pin FPC MIPI CSI-2** | Shared `sunxi-csi` & ISP |
| **NPU AI Engine** | 3.0 TOPs @ `0x03600000` | **3.0 TOPs @ `0x03600000`** | 100% Identical |
| **VPU Video Engine** | H.264/H.265 @ `0x01C0E000` | **H.264/H.265 @ `0x01C0E000`** | 100% Identical |
| **Real-Time RISC-V** | XuanTie E907 Remoteproc | **XuanTie E907 Remoteproc** | 100% Identical |

---

## 4. Software Bring-Up & Mainline Architecture Sharing

Because the **Cubie A7Z** uses the exact same Allwinner A733 silicon as the **Cubie A7A**, 100% of our bring-up work applies directly:
1. **Boot Chain**: Identical 2-stage hybrid boot (`boot0` LPDDR5 dynamic PHY training + Mainline TF-A BL31 + U-Boot 2026.01).
2. **Clock Tree & Base Registers**: Shared Main CCU (`0x02002000`), PRCM R_CCU (`0x07010000`), PRCM R_PIO (`0x07025000`), PMIC I2C (`0x07083000`).
3. **Interrupt Table**: Identical GICv3 interrupt map (GIC-600 @ `0x03400000`/`0x03460000`).
4. **Device Tree**: `sun60i-a733-cubie-a7z.dts` simply disables the `gmac0`/`gmac1` nodes and routes USB over Type-C ports.
