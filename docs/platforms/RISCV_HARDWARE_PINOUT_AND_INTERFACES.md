# RISC-V Dedicated Hardware Pinout, SPI Bus & Interface Architecture

This document defines the hardware resource partitioning, 40-pin GPIO header assignments, voltage domain configurations, and bare-metal register programming model for the **XuanTie E907 RISC-V co-processor** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Hardware Resource Partitioning Matrix

To guarantee hard real-time determinism with zero kernel contention, all hardware interfaces below are released from Linux (`status = "disabled"` in the Device Tree) and assigned exclusively to the RISC-V core:

| Subsystem | Function | SoC IP & Physical Address | 40-Pin Header Physical Pins | Allwinner Pin Names | Logic Voltage |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Telemetry / Serial** | **UART2** (Dedicated RISC-V Console / CRSF) | `0x02500800` | **Pin 11 (TX)**<br>**Pin 13 (RX)** | `PB0`<br>`PB1` | **3.3V** (`reg_cldo3`) |
| **High-Speed Bus** | **SPI0-MOSI / IO0** (Dual-SPI Data 0) | `0x04025000` | **Pin 19** | `PC2` | **3.3V** (`reg_cldo1`) |
| **High-Speed Bus** | **SPI0-MISO / IO1** (Dual-SPI Data 1) | `0x04025000` | **Pin 21** | `PC4` | **3.3V** (`reg_cldo1`) |
| **High-Speed Bus** | **SPI0-CLK** (Clock up to 100 MHz) | `0x04025000` | **Pin 23** | `PC12` | **3.3V** (`reg_cldo1`) |
| **Chip Select 0** | **SPI0-CS0** (FPGA Dual-SPI Link) | `0x04025000` | **Pin 24** | `PC3` | **3.3V** (`reg_cldo1`) |
| **Chip Select 1** | **SPI0-CS1** (IMU Single Full-Duplex Link) | `0x04025000` | **Pin 26** | `PC7` | **3.3V** (`reg_cldo1`) |
| **Interrupt Input** | **FPGA Frame Ready ISR** | Main PIO (`0x02000000`) | **Pin 22** | GPIO Input | **3.3V** |
| **Interrupt Input** | **IMU Data Ready (DRDY) ISR** | Main PIO (`0x02000000`) | **Pin 29** | GPIO Input | **3.3V** |

---

## 2. 40-Pin Header Physical Layout

```text
                           RADXA CUBIE A5E — 40-PIN EXPANSION HEADER
                           
                               3.3V Power [ 1] [ 2] 5V Power (Input/Output)
                         I2C1-SDA (TWI1)  [ 3] [ 4] 5V Power (Input/Output)
                         I2C1-SCL (TWI1)  [ 5] [ 6] GND
                         SPI2-CLK         [ 7] [ 8] UART0-TX (Linux Debug Console)
                                      GND [ 9] [10] UART0-RX (Linux Debug Console)
        [RISC-V]         UART2-TX (PB0)   [11] [12] SPI1-CS0 (Expansion)
        [RISC-V]         UART2-RX (PB1)   [13] [14] GND
                         SPI2-CS0         [15] [16] SPI2-MOSI
                               3.3V Power [17] [18] SPI2-MISO
        [RISC-V]         SPI0-MOSI/IO0    [19] [20] GND
        [RISC-V]         SPI0-MISO/IO1    [21] [22] FPGA Frame Ready ISR (GPIO)   [RISC-V]
        [RISC-V]         SPI0-CLK         [23] [24] SPI0-CS0 (FPGA Dual-SPI)      [RISC-V]
                                      GND [25] [26] SPI0-CS1 (IMU Single-SPI)     [RISC-V]
                         I2C3-SDA (TWI3)  [27] [28] I2C3-SCL (TWI3)
        [RISC-V]         IMU DRDY ISR     [29] [30] GND
                         GPIO / PWM       [31] [32] GPIO / PWM
                         GPIO             [33] [34] GND
                         SPI1-MISO        [35] [36] GPIO
                         GPIO             [37] [38] SPI1-MOSI
                                      GND [39] [40] SPI1-CLK
```

---

## 3. Coexistence of Dual-SPI and Single-SPI on SPI0

### Can Dual-Mode and Single-Mode be used on the same SPI bus?
**Yes.** The Allwinner SPI controller hardware (`0x04025000`) is configured dynamically **per transaction** via the **Transfer Control Register (`SPI_TCR`)**:

1. **Transaction with FPGA (Chip Select 0 / Pin 24):**
   * Assert `CS0` (`PC3`).
   * Set `SPI_TCR` bit for **Dual-IO Mode**.
   * `PC2` (MOSI) and `PC4` (MISO) become bidirectional data lines **IO0** and **IO1**, streaming telemetry and motor commands at double data rates.
   * Deassert `CS0`.

2. **Transaction with IMU (Chip Select 1 / Pin 26):**
   * Assert `CS1` (`PC7`).
   * Clear `SPI_TCR` Dual-IO bit to operate in **Standard Full-Duplex Single-SPI Mode**.
   * `PC2` functions as standard output (MOSI/SDI) and `PC4` functions as standard input (MISO/SDO).
   * Deassert `CS1`.

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │                    XuanTie E907 SPI0 Controller                        │
 └───────┬───────────────────────┬───────────────────────┬────────┬───────┘
         │ PC12 (CLK)            │ PC2 (IO0 / MOSI)      │        │
         │                       │ PC4 (IO1 / MISO)      │        │
         │                       │                       │        │
         ├───────────────────────┼──────────┐            │        │
         │                       │          │            │        │
         ▼                       ▼          │            │        │
   ┌───────────┐           ┌───────────┐    │            │        │
   │   FPGA    │           │    IMU    │    │ PC3 (CS0)  │        │
   │ (Dual IO) │           │ (Standard)│    │            │        │
   └─────▲─────┘           └─────▲─────┘    │            │        │
         │                       │          │            │        │
         └───────────────────────┼──────────┘            │ PC7    │
                                 │                       │ (CS1)  │
                                 └───────────────────────┘        │
```

---

## 4. Voltage Domain Configuration (Port C @ 3.3V)

Port C (`PC2`, `PC3`, `PC4`, `PC7`, `PC12`) is energized by the PMIC LDO **`reg_cldo1`** (`vcc-pc-supply`).

* **Base DTS Default:** 1.8V (for eMMC).
* **Flight Stack Overlay Override:** Configured to **3.3V** (`3300000 µV`), enabling native 3.3V CMOS compatibility across all 40-pin header SPI and GPIO connections without level-shifting circuitry.

---

## 5. Bare-Metal RISC-V Register Setup

```c
#include <stdint.h>
#include <stdbool.h>

#define PIO_BASE        0x02000000
#define PC_CFG0         (*(volatile uint32_t *)(PIO_BASE + 0x0060))
#define PC_CFG1         (*(volatile uint32_t *)(PIO_BASE + 0x0064))

#define SPI0_BASE       0x04025000
#define SPI0_TCR        (*(volatile uint32_t *)(SPI0_BASE + 0x04))
#define SPI0_FCR        (*(volatile uint32_t *)(SPI0_BASE + 0x08))
#define SPI0_FSR        (*(volatile uint32_t *)(SPI0_BASE + 0x0C))
#define SPI0_TXD        (*(volatile uint32_t *)(SPI0_BASE + 0x200))
#define SPI0_RXD        (*(volatile uint32_t *)(SPI0_BASE + 0x300))

void riscv_init_port_c_spi0(void) {
    // Configure PC2 (MOSI), PC3 (CS0), PC4 (MISO), PC7 (CS1) to Mux 4 (SPI0)
    PC_CFG0 &= ~((0xF << 8) | (0xF << 12) | (0xF << 16) | (0xF << 28));
    PC_CFG0 |=  ((0x4 << 8) | (0x4 << 12) | (0x4 << 16) | (0x4 << 28));

    // Configure PC12 (CLK) to Mux 4 (SPI0)
    PC_CFG1 &= ~(0xF << 16);
    PC_CFG1 |=  (0x4 << 16);
}

// Transfer full-duplex Dual-SPI payload with FPGA (CS0)
void riscv_spi0_transceive_fpga_dual(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    // Select CS0, Enable Dual-IO Mode (IO0/IO1)
    SPI0_TCR = (0 << 4) | (1 << 28); // SS_SEL=CS0, DUAL_MODE_EN=1
    
    // ... Push data to SPI0_TXD, poll SPI0_FSR, read SPI0_RXD ...
}

// Transfer standard Single-SPI payload with IMU (CS1)
void riscv_spi0_transceive_imu_single(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    // Select CS1, Disable Dual-IO Mode (Standard MOSI/MISO)
    SPI0_TCR = (1 << 4) | (0 << 28); // SS_SEL=CS1, DUAL_MODE_EN=0
    
    // ... Push data to SPI0_TXD, poll SPI0_FSR, read SPI0_RXD ...
}
```
