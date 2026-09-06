# 🚁 Radxa Cubie A5E + Sipeed Tang Primer 25K Avionics Carrier

This directory contains the complete **KiCad hardware design files, schematics, PCB layout, and manufacturing package** for the custom flight controller carrier board uniting the **Radxa Cubie A5E SBC** (Allwinner A527 / T527) and the **Sipeed Tang Primer 25K FPGA SOM** (Gowin GW5A-LV25).

---

## 1. System Architecture Blueprint

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                   RADXA CUBIE A5E + TANG PRIMER 25K AVIONICS CARRIER                    │
 └────────────────────────────────────────────┬────────────────────────────────────────────┘
                                              │
       ┌──────────────────────────────────────┴──────────────────────────────────────┐
       ▼                                                                             ▼
 ┌───────────────────────────────────────────┐                 ┌───────────────────────────┐
 │          RADXA CUBIE A5E SBC              │                 │   TANG PRIMER 25K SOM     │
 │       (40-Pin Receptacle Below)           │                 │ (Dual 60P BTB On Top)     │
 ├───────────────────────────────────────────┤                 ├───────────────────────────┤
 │ • 8x Cortex-A55 Linux (Vision, iNAV, ML)  │    Dual-SPI     │ • Gowin GW5A-LV25 FPGA    │
 │ • XuanTie E907 RISC-V (Hard Real-Time I/O)│◄───────────────►│ • 8x DShot300/600/1200    │
 │ • UART0: Dedicated Linux Debug Console    │  (up to 100MHz) │ • High-Speed Serial / PWM │
 │ • UART2: Real-Time GPS / CRSF Stream      │  & Frame IRQ    │ • Optical Flow Ingestion  │
 │ • I2C3: Barometer / Magnetometer / Power  │                 │ • Low-Latency Failsafe    │
 └───────────────────────────────────────────┘                 └───────────────────────────┘
       │                                                                             │
       ▼                                                                             ▼
 ┌───────────────────────────────────────────┐                 ┌───────────────────────────┐
 │        SOFT-MOUNTED IMU PAD               │                 │     8x ESC MOTOR HEADERS  │
 │ • 15x15mm Silicone Vibration Gel Pad      │                 │ • DShot / PWM 1 to 8      │
 │ • Dedicated Ultra-Low-Noise 3.3V LDO      │                 │ • Driven by FPGA I/O      │
 │ • SPI0-CS1 (PC7) + IMU_DRDY (PJ24)        │                 └───────────────────────────┘
 └───────────────────────────────────────────┘
```

---

## 2. Onboard Connectors & Interface Mapping

| Designator | Connector Type | Function / Interface | Target Pins / Signals |
| :---: | :--- | :--- | :--- |
| **`J1`** | **40-Pin 2.54mm Socket** | Cubie A5E Stacking Header | Full 40-pin A5E Header (Pins 1–40) |
| **`J2` / `J3`** | **Dual 60-Pin BTB Sockets** | Tang Primer 25K SOM Mount | 14.0 mm center-to-center spacing, 0.4mm pitch |
| **`J4`** | **JST-GH 6-Pin / Solder Pads**| Soft-Mounted IMU Port | `3V3_IMU`, `GND`, `SCK` (`PC12`), `MISO` (`PC4`), `MOSI` (`PC2`), `CS1` (`PC7`), `DRDY` (`PJ24`) |
| **`J5`** | **JST-GH 6-Pin** | GPS + Compass Module | `5V_CLEAN`, `GND`, `UART2_TX` (`PB0`), `UART2_RX` (`PB1`), `I2C3_SCL` (`PB5`), `I2C3_SDA` (`PB4`) |
| **`J6`** | **JST-GH 4-Pin** | RC Receiver (CRSF / ELRS) | `5V_CLEAN`, `GND`, `UART2_RX` / FPGA Serial, Telemetry |
| **`J7`** | **3-Pin 2.54mm Header** | Linux Debug Serial Shell | `UART0_TX` (`PB9`), `UART0_RX` (`PB10`), `GND` (115200 8N1) |
| **`J8`** | **2×8 2.54mm / JST-GH 8-Pin** | 8× ESC Motor Outputs | FPGA I/O DShot channels 1 to 8 + `GND` |
| **`J9`** | **2-Pin XT30 / Screw Terminal**| Main 5V BEC Power Input | 5V Input (from 2S–6S external BEC) + `GND` |

---

## 3. Power & Filtering Subsystem

1. **5V BEC Input Stage**:
   * **TVS Diode**: `SMBJ6.0A` bidirectional 600W TVS clamp (protects against inductive motor braking spikes).
   * **LC Filter**: 0805 3A Power Ferrite Bead + 47 µF 16V X7R ceramic capacitor + 10 µF + 100 nF decoupling.
2. **Dedicated Ultra-Low-Noise 3.3V Sensor LDO**:
   * **Regulator**: SGMicro `SGM2036-3.3YN5G` (or TI `LP5907-3.3`) featuring **75 dB PSRR** and **< 10 µV RMS noise**.
   * Completely decouples the IMU and Barometer power rail from digital CPU/FPGA switching noise.

---

## 4. Mechanical Specifications

* **PCB Dimensions**: **65.0 mm × 56.0 mm** (Matches standard Raspberry Pi HAT / Cubie A5E form factor).
* **Mounting Holes**: 4× M2.5 holes at **58.0 mm × 49.0 mm** spacing.
* **Tang Primer 25K Retention**:
  * Dual **2.5 mm × 8.0 mm routed cutouts** flanking the SOM footprint for a silicone strap, Velcro band, or miniature zip-tie.
* **IMU Soft-Mount Zone**:
  * 15.0 mm × 15.0 mm clean zone on top copper with zero high-profile components, dedicated to a 3M silicone dampening gel pad.

---

## 5. Turnkey Manufacturing with JLCPCB / PCBWay

All components are selected from the **JLCPCB / LCSC Basic & Preferred Parts Library** for low-cost, 1-click automated SMT assembly:

```bash
# Export manufacturing package (Gerber zip, bom.csv, positions.csv)
python3 generate_fabrication_package.py
```
Upload the resulting `carrier_a5e_tang25k_jlcpcb.zip` directly to [jlcpcb.com](https://jlcpcb.com).
