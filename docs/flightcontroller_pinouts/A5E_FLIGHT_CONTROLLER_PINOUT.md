# 🚁 Hardware Carrier & Pinout Specification: Radxa Cubie A5E Flight Controller

This document provides the authoritative, hardware-level schematic and PCB layout pinout specification for building a custom avionics carrier board / flight controller interface board for the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Architectural Interface Partitioning

To ensure hard real-time determinism with zero Linux kernel scheduling jitter, peripheral hardware is split between the **XuanTie E907 RISC-V Coprocessor (Hard Real-Time I/O)** and the **8× Cortex-A55 Linux Host (High-Level Navigation & Vision)**:

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                              RADXA CUBIE A5E HARDWARE CARRIER                          │
 └───────────────────────────────────────────┬────────────────────────────────────────────┘
                                             │
             ┌───────────────────────────────┴───────────────────────────────┐
             ▼                                                               ▼
 ┌───────────────────────────────────────┐                       ┌───────────────────────┐
 │   XuanTie E907 RISC-V Coprocessor     │                       │ 8x Cortex-A55 Linux   │
 │   (Hard Real-Time Sensor / Actuator)  │                       │ (iNAV, Vision, Comms) │
 ├───────────────────────────────────────┤                       ├───────────────────────┤
 │ • SPI0: Dual-SPI Link to FPGA (CS0)   │                       │ • UART0: Debug Shell  │
 │ • SPI0: Single-SPI to IMU (CS1)       │                       │ • USB 3.0 / PCIe      │
 │ • UART2: High-Speed Telemetry/GPS/CRSF│                       │ • Wi-Fi 6 (AIC8800)   │
 │ • I2C3: Baro / Mag / Power Monitor    │                       │ • Dual GbE DWMAC      │
 │ • 4x PIO External Interrupts (EINT)   │                       │ • MIPI CSI-2 Cameras  │
 └───────────────────────────────────────┘                       └───────────────────────┘
```

---

## 2. 40-Pin Header Complete Pinout & Schematic Table

The standard 40-pin 2.54mm expansion header (`J11`) is mapped as follows for the custom carrier board:

| Pin # | Signal Name | SoC Pin | Hardware Function | Subsystem Domain | Voltage | Recommended Carrier Termination / Protection |
| :---: | :--- | :---: | :--- | :--- | :---: | :--- |
| **1** | `3V3_SYS` | — | 3.3V System Power Output | Power | 3.3V | Decoupling: 10µF + 0.1µF to GND |
| **2** | `5V_SYS` | — | 5.0V System Power Input/Output | Power | 5.0V | 2A rated copper trace / TVS clamp |
| **3** | `I2C1_SDA` | `PB6` | Primary I2C Data (or Linux) | Linux / Shared | 3.3V | 4.7 kΩ pull-up to 3.3V, ESD diode |
| **4** | `5V_SYS` | — | 5.0V System Power Input/Output | Power | 5.0V | Tie with Pin 2 |
| **5** | `I2C1_SCL` | `PB7` | Primary I2C Clock (or Linux) | Linux / Shared | 3.3V | 4.7 kΩ pull-up to 3.3V, ESD diode |
| **6** | `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **7** | `SPI2_CLK` | `PB2` | Secondary SPI Clock (Optional) | E907 / Linux | 3.3V | 33 Ω series damping resistor |
| **8** | `UART0_TX` | `PB9` | **Linux Debug Shell TX (115200 8N1)**| **Linux Host** | 3.3V | Route to Debug Header / USB-UART |
| **9** | `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **10**| `UART0_RX` | `PB10`| **Linux Debug Shell RX (115200 8N1)**| **Linux Host** | 3.3V | Route to Debug Header / USB-UART |
| **11**| `UART2_TX` | `PB0` | **Real-Time GPS / Telemetry TX** | **E907 RISC-V**| 3.3V | Route to GPS / CRSF Receiver TX |
| **12**| `NC / RESERVED` | `PH4` | *Do not connect (Port H / PCIe)* | *Unused* | — | Leave Floating (Prevents PCIe conflicts) |
| **13**| `UART2_RX` | `PB1` | **Real-Time GPS / Telemetry RX** | **E907 RISC-V**| 3.3V | Route to GPS / CRSF Receiver RX |
| **14**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **15**| `SPI2_CS0` | `PB3` | Secondary SPI Chip Select 0 | E907 / Linux | 3.3V | 10 kΩ pull-up to 3.3V |
| **16**| `SPI2_MOSI`| `PB0` | Secondary SPI MOSI | E907 / Linux | 3.3V | 33 Ω series damping resistor |
| **17**| `3V3_SYS` | — | 3.3V System Power Output | Power | 3.3V | 100nF decoupling |
| **18**| `SPI2_MISO`| `PB1` | Secondary SPI MISO | E907 / Linux | 3.3V | 33 Ω series damping resistor |
| **19**| `SPI0_MOSI`| `PC2` | **Dual-SPI IO0 / Single MOSI** | **E907 RISC-V**| 3.3V | 33 Ω damping, route to FPGA / IMU |
| **20**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **21**| `SPI0_MISO`| `PC4` | **Dual-SPI IO1 / Single MISO** | **E907 RISC-V**| 3.3V | 33 Ω damping, route to FPGA / IMU |
| **22**| `IRQ_FPGA_RDY`| `PC15`| **FPGA Frame Ready External IRQ** | **E907 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **23**| `SPI0_CLK` | `PC12`| **Dual/Single SPI Clock (up to 100MHz)**| **E907 RISC-V**| 3.3V | 33 Ω damping, length match to IO0/IO1 |
| **24**| `SPI0_CS0` | `PC3` | **FPGA Dual-SPI Chip Select 0** | **E907 RISC-V**| 3.3V | 10 kΩ pull-up to 3.3V, Active LOW |
| **25**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **26**| `SPI0_CS1` | `PC7` | **IMU Single-SPI Chip Select 1** | **E907 RISC-V**| 3.3V | 10 kΩ pull-up to 3.3V, Active LOW |
| **27**| `I2C3_SDA` | `PB4` | **Avionics Sensor I2C Data** | **E907 RISC-V**| 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **28**| `I2C3_SCL` | `PB5` | **Avionics Sensor I2C Clock** | **E907 RISC-V**| 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **29**| `IRQ_IMU_DRDY`| `PJ24`| **IMU Data Ready (DRDY) IRQ** | **E907 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **30**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **31**| `IRQ_GEN1` | `PJ25`| **General Purpose PIO IRQ 1** | **E907 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **32**| `IRQ_GEN2` | `PD20`| **General Purpose PIO IRQ 2** | **E907 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **33**| `GPIO_USER1`| `PD21`| User Configurable GPIO / PWM | E907 / Linux | 3.3V | ESD diode |
| **34**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **35**| `NC / RESERVED` | `PH6` | *Do not connect (Port H / PCIe)* | *Unused* | — | Leave Floating (Prevents PCIe conflicts) |
| **36**| `GPIO_USER2`| `PD22`| User Configurable GPIO / PWM | E907 / Linux | 3.3V | ESD diode |
| **37**| `GPIO_USER3`| `PD23`| User Configurable GPIO / PWM | E907 / Linux | 3.3V | ESD diode |
| **38**| `NC / RESERVED` | `PH5` | *Do not connect (Port H / PCIe)* | *Unused* | — | Leave Floating (Prevents PCIe conflicts) |
| **39**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **40**| `NC / RESERVED` | `PH7` | *Do not connect (Port H / PCIe)* | *Unused* | — | Leave Floating (Prevents PCIe conflicts) |

---

## 3. PCB Layout & Signal Routing Rules

### 3.1 High-Speed SPI0 (FPGA & IMU Bus)
* **Signals**: `PC12` (CLK), `PC2` (IO0/MOSI), `PC4` (IO1/MISO), `PC3` (CS0), `PC7` (CS1).
* **Impedance**: Single-ended 50 Ω ± 10%.
* **Length Matching**: Match `PC12` (CLK) to `PC2` (IO0) and `PC4` (IO1) within **± 1.0 mm (± 40 mils)** to support clock rates up to 100 MHz in Dual-SPI mode.
* **Series Damping**: Place 22 Ω – 33 Ω series damping resistors close to the driving pins.
* **Ground Reference**: Route continuously over an uninterrupted ground plane; avoid crossing split power planes.

### 3.2 Dual Serial Port Isolation
* **Linux Debug Terminal (`UART0`)**:
  * Route `PB9` (TX) and `PB10` (RX) with `GND` to a dedicated 3-pin 1.25mm / 2.54mm header for field diagnostic connection.
* **Real-Time Coprocessor Telemetry (`UART2`)**:
  * Route `PB0` (TX) and `PB1` (RX) directly to the on-board GPS / CRSF receiver connector.
  * Keep traces short and shielded by ground fills to prevent RF ingress from radio transmitters.

### 3.3 Avionics I2C3 Bus
* **Signals**: `PB4` (SDA), `PB5` (SCL).
* **Pull-Up Resistors**: Place 2.2 kΩ pull-ups to `3V3_SYS` near the barometric pressure / magnetometer sensors.
* **Capacitive Loading**: Keep total bus capacitance below 200 pF to allow 400 kHz / 1 MHz I2C Fast-Mode Plus operation.

### 3.4 External Interrupt (EINT) Lines
* **Signals**: `PC15` (FPGA Ready), `PJ24` (IMU DRDY), `PJ25`, `PD20`.
* **Filtering**: Place 10 pF – 22 pF low-pass capacitors close to the SoC inputs to reject high-frequency motor switching EMI without degrading edge sharpness (< 10 ns rise time).

---

## 4. Port H & PCIe Carrier Board Coexistence Rule

> [!IMPORTANT]
> **PCIe Sideband & Port H Rule**:
> If designing a carrier board for the Allwinner T527 Compute Module with an **M.2 PCIe NVMe slot**:
> 1. Port H pins (`PH11` PERST#, `PH12` WAKE#, `PH19` CLKREQ#) are dedicated to the M.2 PCIe slot.
> 2. Do **not** connect 40-pin header Pins 12, 35, 38, or 40 (`PH4`, `PH6`, `PH5`, `PH7`).
> 3. Following this specification guarantees 100% simultaneous operation of **M.2 PCIe NVMe SSDs** on Linux and **Hard Real-Time SPI0/UART2/I2C3/EINT** on the XuanTie E907.
