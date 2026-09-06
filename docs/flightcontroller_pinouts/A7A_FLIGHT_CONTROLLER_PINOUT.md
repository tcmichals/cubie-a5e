# 🚀 Hardware Carrier & Pinout Specification: Radxa Cubie A7A Flight Controller

This document provides the authoritative, hardware-level schematic and PCB layout pinout specification for building a custom avionics carrier board / flight controller interface board for the **Radxa Cubie A7A (Allwinner A733 / `sun60i`)**.

---

## 1. Architectural Interface Partitioning

Peripheral hardware is partitioned between the **XuanTie E902 RISC-V Coprocessor (Hard Real-Time I/O)** and the **2× A76 + 6× A55 Linux DynamIQ Host (High-Level Navigation, Vision, AI)**:

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                              RADXA CUBIE A7A HARDWARE CARRIER                          │
 └───────────────────────────────────────────┬────────────────────────────────────────────┘
                                             │
             ┌───────────────────────────────┴───────────────────────────────┐
             ▼                                                               ▼
 ┌───────────────────────────────────────┐                       ┌───────────────────────┐
 │   XuanTie E902 RISC-V Coprocessor     │                       │ 8-Core ARM Linux Host │
 │   (Hard Real-Time Sensor / Actuator)  │                       │ (iNAV, Vision, Comms) │
 ├───────────────────────────────────────┤                       ├───────────────────────┤
 │ • SPI1: Dual/Single SPI Link to FPGA  │                       │ • UART0: Debug Shell  │
 │ • UART2: High-Speed Telemetry/GPS/CRSF│                       │ • PCIe 3.0 (16-pin J3)│
 │ • I2C2/I2C7: Baro / Mag / Power Mon   │                       │ • USB 3.1 DWC3 Gen2   │
 │ • 4x PIO External Interrupts (PL/PJ)  │                       │ • Wi-Fi 6 (AIC8800)   │
 │ • SRAM A2 Ringbuffer Stream to Linux  │                       │ • Dual GbE DWMAC      │
 └───────────────────────────────────────┘                       └───────────────────────┘
```

---

## 2. 40-Pin Header Complete Pinout & Schematic Table

The standard 40-pin 2.54mm expansion header (`J11`) is mapped as follows:

| Pin # | Signal Name | SoC Pin | Hardware Function | Subsystem Domain | Voltage | Recommended Carrier Termination / Protection |
| :---: | :--- | :---: | :--- | :--- | :---: | :--- |
| **1** | `3V3_SYS` | — | 3.3V System Power Output | Power | 3.3V | Decoupling: 10µF + 0.1µF to GND |
| **2** | `5V_SYS` | — | 5.0V System Power Input/Output | Power | 5.0V | 2A rated copper trace / TVS clamp |
| **3** | `I2C7_SDA` | `PB6` | Secondary I2C Data | E902 / Linux | 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **4** | `5V_SYS` | — | 5.0V System Power Input/Output | Power | 5.0V | Tie with Pin 2 |
| **5** | `I2C7_SCL` | `PB7` | Secondary I2C Clock | E902 / Linux | 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **6** | `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **7** | `GPIO2_B3` | `PB2` | General Purpose GPIO / SPI2-CLK | E902 / Linux | 3.3V | 33 Ω series resistor |
| **8** | `CPUX_TX` | `PB9` | **Linux Debug Shell TX (115200 8N1)**| **Linux Host** | 3.3V | Route to Debug Header / USB-UART |
| **9** | `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **10**| `CPUX_RX` | `PB10`| **Linux Debug Shell RX (115200 8N1)**| **Linux Host** | 3.3V | Route to Debug Header / USB-UART |
| **11**| `UART2_TX` | `PB0` | **Real-Time GPS / Telemetry TX** | **E902 RISC-V**| 3.3V | Route to GPS / CRSF Receiver TX |
| **12**| `I2S4_BCLK` | `PK0` | Audio BCLK / GPIO | Linux / Audio | 3.3V | Level shifted via UM3304 |
| **13**| `UART2_RX` | `PB1` | **Real-Time GPS / Telemetry RX** | **E902 RISC-V**| 3.3V | Route to GPS / CRSF Receiver RX |
| **14**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **15**| `IRQ_PL7` | `PL7` | **Real-Time PIO IRQ 1 (IMU DRDY)**| **E902 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **16**| `IRQ_PJ24` | `PJ24`| **Real-Time PIO IRQ 2 (FPGA Ready)**| **E902 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **17**| `3V3_SYS` | — | 3.3V System Power Output | Power | 3.3V | 100nF decoupling |
| **18**| `IRQ_PJ25` | `PJ25`| **Real-Time PIO IRQ 3 (General)** | **E902 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **19**| `SPI1_MOSI`| `PB11`| **Dual-SPI IO0 / Single MOSI** | **E902 RISC-V**| 3.3V | 33 Ω damping, route to FPGA / IMU |
| **20**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **21**| `SPI1_MISO`| `PB12`| **Dual-SPI IO1 / Single MISO** | **E902 RISC-V**| 3.3V | 33 Ω damping, route to FPGA / IMU |
| **22**| `IRQ_PL10` | `PL10`| **Real-Time PIO IRQ 4 (General)** | **E902 RISC-V**| 3.3V | 10 kΩ pull-down, ESD clamp |
| **23**| `SPI1_CLK` | `PB13`| **Dual/Single SPI Clock** | **E902 RISC-V**| 3.3V | 33 Ω damping, length match to IO0/IO1 |
| **24**| `SPI1_CS0` | `PB14`| **FPGA Dual-SPI Chip Select 0** | **E902 RISC-V**| 3.3V | 10 kΩ pull-up to 3.3V, Active LOW |
| **25**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **26**| `SPI1_CS1 / PD14`| `PD14`| **IMU Single-SPI Chip Select 1** | **E902 RISC-V**| 3.3V | 10 kΩ pull-up to 3.3V, Active LOW |
| **27**| `I2C2_SDA` | `PB4` | **Avionics Sensor I2C Data** | **E902 RISC-V**| 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **28**| `I2C2_SCL` | `PB5` | **Avionics Sensor I2C Clock** | **E902 RISC-V**| 3.3V | 2.2 kΩ pull-up to 3.3V, ESD diode |
| **29**| `GPIO_PK5` | `PK5` | User GPIO | E902 / Linux | 3.3V | Level shifted via UM3204H |
| **30**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **31**| `GPIO_PK6` | `PK6` | User GPIO | E902 / Linux | 3.3V | Level shifted via UM3204H |
| **32**| `GPIO_PD20`| `PD20`| User Configurable GPIO / PWM | E902 / Linux | 3.3V | ESD diode |
| **33**| `GPIO_PK7` | `PK7` | User GPIO | E902 / Linux | 3.3V | Level shifted via UM3204H |
| **34**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **35**| `I2S4_LRCLK`| `PK1` | Audio LRCLK / GPIO | Linux / Audio | 3.3V | Level shifted via UM3304 |
| **36**| `I2S4_MCLK` | `PK2` | Audio MCLK / GPIO | Linux / Audio | 3.3V | Level shifted via UM3304 |
| **37**| `GPIO_PK8` | `PK8` | User GPIO | E902 / Linux | 3.3V | Level shifted via UM3204H |
| **38**| `I2S4_DIN`  | `PK3` | Audio Data In / GPIO | Linux / Audio | 3.3V | Level shifted via UM3304 |
| **39**| `GND` | — | Ground | Ground | 0V | Ground Plane Return |
| **40**| `I2S4_DOUT` | `PK4` | Audio Data Out / GPIO | Linux / Audio | 3.3V | Level shifted via UM3304 |

---

## 3. Dedicated PCIe 3.0 Connector (`J3` - 16-Pin FPC)

The Radxa Cubie A7A exposes PCIe 3.0 x1 on a dedicated 16-pin 0.5mm pitch FPC connector (`J3`):

| FPC Pin # | Net Name | Signal Function | Direction |
| :---: | :--- | :--- | :---: |
| **1** | `GND` | Ground Reference | — |
| **2** | `PCIE1-CLKP` | PCIe Differential Reference Clock (+) | Output to Device |
| **3** | `PCIE1-CLKN` | PCIe Differential Reference Clock (-) | Output to Device |
| **4** | `GND` | Ground Reference | — |
| **5** | `PCIE1-RX0P` | PCIe Receive Differential Pair (+) | Input to SoC |
| **6** | `PCIE1-RX0N` | PCIe Receive Differential Pair (-) | Input to SoC |
| **7** | `GND` | Ground Reference | — |
| **8** | `PCIE1-TX0P` | PCIe Transmit Differential Pair (+) | Output from SoC |
| **9** | `PCIE1-TX0N` | PCIe Transmit Differential Pair (-) | Output from SoC |
| **10**| `GND` | Ground Reference | — |
| **11**| `PCIE_PWR_EN` | PCIe Power Enable (`PE11`) | Output to Regulator |
| **12**| `PCIE-PERSTn` | PCIe Fundamental Reset (`PE13`) | Output to Device |
| **13**| `PCIE-CLKREQn`| PCIe Clock Request (`PE14`) | Bidirectional |
| **14**| `PCIE-WAKEn` | PCIe Power Management Wake (`PE12`)| Input to SoC |
| **15**| `PCIE_3V3` | 3.3V Power Rail | Power |
| **16**| `PCIE_3V3` | 3.3V Power Rail | Power |

---

## 4. Hardware Layout Guidelines for A7A

1. **Complete Independence**: Because PCIe is entirely routed on `J3` (using Bank `PE`), **none of the 40-pin header pins are constrained by PCIe**.
2. **Dual Serial Architecture**:
   - `PB9`/`PB10` (Pins 8/10) provide the **Linux root debug terminal**.
   - `PB0`/`PB1` (Pins 11/13) provide the **E902 real-time telemetry / GPS port**.
3. **High-Speed SPI1 Bus**:
   - Route `PB11` (MOSI), `PB12` (MISO), `PB13` (CLK), `PB14` (CS0), and `PD14` (CS1) with 50 Ω single-ended impedance and length match within ± 1.0 mm.
