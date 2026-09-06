# 🛩️ Flight Controller Hardware Carrier & Pinout Design Library

This directory contains the hardware-level electrical, schematic, and PCB layout pinout specifications for designing custom flight controller carrier boards and avionics breakout modules.

---

## Board Specifications

| Document | Target SoC | Coprocessor | Real-Time High-Speed Bus | PCIe Interface | Primary Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **[`A5E_FLIGHT_CONTROLLER_PINOUT.md`](./A5E_FLIGHT_CONTROLLER_PINOUT.md)** | **Allwinner A527 / T527** (`sun55i`) | **XuanTie E907** (RV32IMAFDC + Double FPU + DSP @ 200MHz) | **`SPI0` (Port C)**: Dual-SPI FPGA + Single-SPI IMU | M.2 Slot (Carrier Only; isolates Port H) | **Hard Real-Time Autopilot + Sensor Fusion + Vision** |
| **[`A7A_FLIGHT_CONTROLLER_PINOUT.md`](./A7A_FLIGHT_CONTROLLER_PINOUT.md)** | **Allwinner A733** (`sun60iw2`) | **XuanTie E902** (RV32EMC @ 200MHz) | **`SPI1` (Port B)**: Dual/Single SPI FPGA + IMU | **16-pin FPC `J3`** (PCIe 3.0 Gen3 x1) | **High-Compute AI + Vision + Real-Time I/O Front-End** |

---

## Universal Avionics Rules (Both Platforms)

1. **Dual Independent Serial Ports**:
   - **Pins 8 & 10 (`UART0`)**: Dedicated to Linux debug shell (`ttyS0` @ 115200 8N1). Connect to an on-board USB-to-UART bridge or 3-pin field diagnostic header.
   - **Pins 11 & 13 (`UART2`)**: Dedicated to the XuanTie RISC-V Coprocessor for real-time GPS (UBX/NMEA) and RC receiver (CRSF/ELRS) streams.
2. **Dual-Mode High-Speed SPI**:
   - **FPGA Link (CS0)**: Operates in Dual-IO SPI Mode (IO0/IO1) for high-bandwidth bidirectional state streaming.
   - **IMU Link (CS1)**: Operates in standard Single-IO Full-Duplex Mode on the same bus via dynamic hardware switching.
3. **Dedicated PIO Interrupts**:
   - 4 hardware external interrupt lines (EINT) routed directly to RISC-V GPIO interrupts to handle IMU DRDY and FPGA Frame Ready signals with sub-microsecond latency.
4. **PCIe & Port H Isolation (A5E)**:
   - When designing an A5E carrier with M.2 PCIe, do not wire 40-pin header Pins 12, 35, 38, or 40 (`SPI1` / Port H) to eliminate any bus collisions.
