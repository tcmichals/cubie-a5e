# Radxa Cubie A7A USB, PHY, Power & PMIC Architecture Reference

This document provides complete register definitions, hardware schematics, and bring-up analysis for USB 2.0/3.0, PHY transceivers, FE1.1S hub, AIC8800 Wi-Fi/BT, and the dual AXP PMIC power subsystem on the **Radxa Cubie A7A (Allwinner A733 / sun60i)**.

---

## 1. USB Topology Overview

```
                      ┌─────────────────────────────────────────────────────────┐
                      │                 Allwinner A733 SoC                     │
                      │                                                         │
                      │   EHCI0 / OHCI0 (0x04101000 / 0x04101400)               │
                      │        │                                                │
                      │      PHY0 (0x04100400 / 0x04101800)                     │
                      │        │  (USB1-DP / USB1-DM)                           │
                      └────────┼────────────────────────────────────────────────┘
                               │
                               ▼
                   Bottom USB 3.0 / 2.0 Type-A Port (CON_U3_U2)
                   5V VBUS powered by U2 (SGM2576), enabled by PL2 (USB0-DRVVBUS)

                      ┌─────────────────────────────────────────────────────────┐
                      │                 Allwinner A733 SoC                     │
                      │                                                         │
                      │   EHCI1 / OHCI1 (0x04200000 / 0x04200400)               │
                      │        │                                                │
                      │      PHY1 (0x04200800)                                  │
                      │        │  (USB2-DP / USB2-DM)                           │
                      └────────┼────────────────────────────────────────────────┘
                               │
                               ▼
                   FE1.1S 4-Port USB 2.0 Hub (U6)
                   5V VBUS powered by U5 (SGM2576), enabled by PM5 (USB_HOST_EN)
                   Hub 3.3V power: DCDC1 via R58 (0R) -> VCC_3V3_USB20HUB
                               │
            ┌──────────────────┼──────────────────┐
            ▼                  ▼                  ▼
     Downstream Port 1   Downstream Ports 2 & 3  Downstream Port 4
     Top USB Type-A Port   Internal USB2D0_2     FCU760K / AIC8800 Wi-Fi 6
     (CON1)                Header (J4)           Module (U3)
```

---

## 2. Power Architecture & Voltage Rails

### 2.1 5V System Rail (`VCC5V0_SYS`)
- **Source**: Type-C Power Input (`J16`) via Ferrite Bead `FB2` (60R 5A).
- **Physical Verification**: Measured at **5.24V** on 40-pin header Pins 2 and 4 (Pin 6 is GND).
- **Supplies**:
  - `U2` Pin 5 (`IN`) -> SGM2576 Power Switch for Bottom Type-A port (`VCC5V0_USB30_OTG`).
  - `U5` Pin 5 (`IN`) -> SGM2576 Power Switch for Top Type-A port & FE1.1S Hub (`VCC5V0_USB20`).
  - `AXP PMIC` DCIN pins.

### 2.2 GPIO Bank Power (`VCC-PL` / `VCC-PM`)
- **Bank PL (pins PL0 - PL12)** and **Bank PM (pins PM0 - PM9)** require an external 3.3V I/O supply rail:
  - `VCC-PL` (ball `1M23`) is supplied by **`ALDO1` (3.3V)** from the **AXP PMIC**.
  - `VCC-PM` (ball `1P22`) is tied to `VCC-PL` via 0-ohm resistor `RA13`.
- **Significance**: If `ALDO1` is not enabled by the PMIC driver, GPIO outputs (`PL2`, `PM0`, `PM1`, `PM5`) cannot source 3.3V to enable `U2`, `U5`, or Wi-Fi FET `Q8`.

### 2.3 Wi-Fi Module Power (`U3` AIC8800)
- `VBAT` (pin 11): 3.3V supplied by `WIFI_3V3` via P-channel MOSFET `Q8` (WPM2015).
- `Q8` gate is pulled up to `VCC-WIFI` and pulled down by NPN transistor `Q9` driven by `PM0` (`USB_WIFI_PWR`).
- `CHIP_EN` (pin 18): Driven by `PM1` (`WL-REG-ON`).

---

## 3. CCU Clock and Reset Registers for USB

| Register Name | Physical Addr | CCU Offset | Config Value | Description |
|:---|:---|:---|:---|:---|
| `USB_SYS_AHB_GATE` | `0x020025a4` | `0x05a4` | `0x00000001` | Bit 0: Master AHB Clock Gate for USB subsystem |
| `RES_DCAP_24M_CLK` | `0x02003a00` | `0x1a00` | `0x00000008` | Bit 3: 24 MHz Reference for PHY Resistance Calibration |
| `USB_REF_MSI_LITE` | `0x02003340` | `0x1340` | `0x80010001` | Bit 31: USB_REF (24M), Bit 16: MSI_LITE2 reset, Bit 0: MSI_LITE2 clk |
| `USB0_PHY_CFG` | `0x02003300` | `0x1300` | `0xc0000000` | Bit 31: PHY0 24M Clock Gate, Bit 30: PHY0 Reset Deassert |
| `USB0_HCI_CFG` | `0x02003304` | `0x1304` | `0x00110011` | Bits 20, 16: EHCI0/OHCI0 Resets, Bits 4, 0: EHCI0/OHCI0 Bus Clocks |
| `USB1_PHY_CFG` | `0x02003308` | `0x1308` | `0xc0000000` | Bit 31: PHY1 24M Clock Gate, Bit 30: PHY1 Reset Deassert |
| `USB1_HCI_CFG` | `0x0200330c` | `0x130c` | `0x00110011` | Bits 20, 16: EHCI1/OHCI1 Resets, Bits 4, 0: EHCI1/OHCI1 Bus Clocks |

---

## 4. USB PHY Transceiver Calibration & PMU Configuration

### 4.1 SYSCFG 200-Ohm Resistor Calibration
Allwinner A733 requires initial resistor calibration in the SYSCFG register block (`0x03000000`):
```bash
devmem 0x03000160 32 0x00000030  # RESCAL Calibration Control
devmem 0x03000164 32 0x00000000  # RES0 Trim Offset
```

### 4.2 PHY PMU Passby & SIDDQ Power-Down Clear
```bash
# Clear SIDDQ power-down bit on PHY0 and PHY1:
devmem 0x04101810 32 0x00000000
devmem 0x04200810 32 0x00000000

# Enable ULPI bypass & AHB burst flags on PHY0 and PHY1 PMUs:
devmem 0x04101800 32 0x0000cf01
devmem 0x04200800 32 0x0000cf01
```

---

## 5. Dual PMIC Subsystem on `R_I2C0` (`0x07083000`)

The board uses two PMICs connected to `R_I2C0` (SoC pins `PL0` / `PL1`):
1. **AXP515 (`0x34`)**:
   - Manages Type-C port, CC logic, and USB Power Delivery (`drivevbus`).
   - Register `0x11` (`AXP515_RBFET_SET`): Bit 6 enables `drivevbus`.
2. **AXP8191 (`0x36`)**:
   - Multi-channel DC-DC and LDO system PMIC.
   - Register `0x20` (`AXP8191_LDO_POWER_ON_OFF_CTL1`): Bit 0 enables `ALDO1` (3.3V for `VCC-PL`/`VCC-PM`).
   - Register `0x24` (`AXP8191_ALDO1OUT_VOL`): Sets `ALDO1` voltage level.

### R_CCU TWI Enable Sequence:
```bash
# Enable R_TWI0 clock (bit 0) and deassert reset (bit 16) in R_CCU (0x0701019c):
devmem 0x0701019c 32 0x00010001

# Enable TWI controller (bit 6 = BUS_EN in 0x0708300c):
devmem 0x0708300c 32 0x00000040
```
