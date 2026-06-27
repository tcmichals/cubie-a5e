# Flight Controller Architecture & AbstractX Integration

This document outlines the flight stack system design for the Radxa Cubie A5E flight controller, detailing the split-responsibility model between the high-level Linux OS and the deterministic FPGA.

---

## 1. Split-Responsibility Architecture

To achieve both high-level intelligence (computer vision, TinyML NPU workloads, route planning) and microsecond-level hard real-time flight control, the system divides responsibilities between two main processing blocks:

```mermaid
graph TD
    subgraph Cubie A5E (Linux Side)
        A[Flight Logic & Mission State] --> B[TinyML/NPU Obstacle Avoidance]
        B --> C[Navigation & SLAM]
    end
    subgraph FPGA (Low-Level Hardware)
        D[DSHOT Motor Outputs]
        E[IMU Sensor Fast Domain]
        F[PWM Input/Output]
    end
    C <== Dual SPI bus (AbstractX) ==> E
```

### A. Cubie A5E (Linux OS Domain)
* **High-Level Flight Logic:** Navigation, mission state machine, and waypoint planning.
* **Intelligent Assist:** Machine learning models running on the onboard NPU (using the TensorFlow Lite TIM-VX delegate) for real-time video processing, obstacle avoidance, and adaptive flight control.
* **Networking & Telemetry:** Wi-Fi (AIC8800), LTE interfaces, and high-bandwidth data logging.

### B. FPGA (Deterministic Hardware Domain)
* **Hard Real-Time I/O:** Generation of time-critical signals (e.g., DSHOT ESC outputs, PWM output for servos, PWM input for RC receivers).
* **IMU Interfacing:** Rapidly reading the inertial sensors and providing a low-latency, deterministic sensor feedback stream.
* **Safety Co-processor:** Running safety watchdogs and failsafe logic if the Linux OS crashes or lags.

---

## 2. Interface Design (AbstractX Framework)

The Cubie A5E communicates with the FPGA over **dual high-speed SPI channels** orchestrated by the **AbstractX** framework.

* **AbstractX Codebase:** [https://github.com/tcmichals/AbstractX](https://github.com/tcmichals/AbstractX)
* **SPI Transport Protocol Specification:** [AbstractX SPI Transport Profile](https://github.com/tcmichals/AbstractX/blob/main/docs/ASP_SPI_TRANSPORT.md)
* **Typical Project Layout:** 
  The AbstractX repository is expected to reside as a sibling to this buildroot repository:
  ```text
  /home/tcmichals/projects/
  ├── cubie-a5e/      <-- This Buildroot repository
  └── AbstractX/      <-- AbstractX framework repository
  ```

### C. Network-over-SPI Option (TUN/TAP Driver)

For advanced telemetry and standard socket-based networking (e.g., routing MAVLink via UDP/IP), you can implement a virtual **TUN/TAP network interface** over the physical SPI link. 

This abstracts the raw SPI transfers into standard TCP/IP networking, allowing you to use standard socket calls (`sendto`/`recvfrom`) to communicate with IP stacks running on the FPGA.

There are two primary integration paths for this driver:
1. **In this Buildroot Repository:** 
   We can package the driver as a custom out-of-tree Linux kernel module under `project-cubie-a5e/package/` (e.g. `br2-spi-tun`). Buildroot will compile this module against the target kernel headers during the build, auto-loading the virtual interface (e.g. `spi0`) on startup.
2. **In the AbstractX Repository:**
   Alternatively, the driver can reside directly in the `AbstractX` repository. This keeps the low-level SPI communication protocol and the virtual network interface codebase coupled together, simplifying compilation when testing standalone application updates.

---

## 3. Communication Profile & Safety Watchdogs

Because Linux is not a hard real-time operating system, communication between the A5E and the FPGA is designed to handle latency spikes and potential software crashes safely:

1. **State Packets:** Compact, fixed-size binary packets are exchanged at a fixed frequency (e.g., 400Hz).
2. **Watchdog Timer:** The FPGA runs a hardware-level watchdog. If a valid SPI control packet is not received from the A5E within a short window (e.g., 50ms), the FPGA automatically enters a **failsafe mode** (e.g., leveling the aircraft or disarming motors).
3. **Sensor Timing Domain:** The FPGA timestamps IMU data at the hardware level before passing it to the A5E, allowing the flight logic to correctly compute state estimation despite OS scheduling jitter.

---

## 4. Hardware Interfaces & Device Tree Mapping

To enable these hardware communication channels, the Buildroot build compiles and applies a custom Device Tree Overlay:
* **DTS Overlay Source:** [`project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-flight-stack.dtso`](file:///home/tcmichals/projects/cubie-a5e/project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-flight-stack.dtso)
* **Boot Time Loading:** During startup, U-Boot loads the compiled `.dtbo` overlay, applies it using `fdt apply` to merge it with the base device tree, and boots the Linux kernel. The kernel then exposes these buses as standard character device nodes in `/dev/`.

### A. Physical 40-Pin GPIO Header Mapping

The following ASCII diagram maps the physical pinout to the configured device nodes on the Cubie A5E board:

```text
                        RADXA CUBIE A5E GPIO HEADER
                        
           3.3V Power [ 1] [ 2] 5V Power (Input/Output)
     I2C1-SDA (TWI1)  [ 3] [ 4] 5V Power (Input/Output)
     I2C1-SCL (TWI1)  [ 5] [ 6] GND
     SPI2-CLK         [ 7] [ 8] UART0-TX (Debug Console Terminal)
                  GND [ 9] [10] UART0-RX (Debug Console Terminal)
     UART2-TX (GPS)   [11] [12] SPI1-CS0 (FPGA Dual-SPI Link B)
     UART2-RX (GPS)   [13] [14] GND
     SPI2-CS0         [15] [16] SPI2-MOSI
           3.3V Power [17] [18] SPI2-MISO
     SPI0-MOSI (IMU)  [19] [20] GND
     SPI0-MISO (IMU)  [21] [22] GPIO
     SPI0-CLK  (IMU)  [23] [24] SPI0-CS0 (IMU Dual-SPI Link A)
                  GND [25] [26] GPIO
     I2C3-SDA (TWI3)  [27] [28] I2C3-SCL (TWI3) (Sensors/Compass)
                 GPIO [29] [30] GND
                 GPIO [31] [32] GPIO/PWM
                 GPIO [33] [34] GND
     SPI1-MISO (FPGA) [35] [36] GPIO
                 GPIO [37] [38] SPI1-MOSI (FPGA)
                  GND [39] [40] SPI1-CLK  (FPGA)
```

### B. Device Nodes Exposed to the Flight Controller

Within your C++ (AbstractX) or Python application code, you access these interfaces via standard Linux device files:

| Interface | Physical Pins | Linux Device File | Purpose |
| :--- | :--- | :--- | :--- |
| **UART0** | Pins 8, 10 | `/dev/ttyS0` | System Debug Serial Console (Terminal) |
| **UART2** | Pins 11, 13 | `/dev/ttyS2` | GPS Receiver Telemetry Input |
| **SPI0** | Pins 19, 21, 23, 24 | `/dev/spidev0.0` | Dual-SPI Channel A (Inertial Sensors / IMU) |
| **SPI1** | Pins 35, 38, 40, 12 | `/dev/spidev1.0` | Dual-SPI Channel B (FPGA Flight Control Link) |
| **SPI2** | Pins 7, 15, 16, 18 | `/dev/spidev2.0` | Third Independent SPI Bus (Peripherals / Payload) |
| **I2C1** | Pins 3, 5 | `/dev/i2c-1` | Auxiliary I2C Bus (TWI1) |
| **I2C3** | Pins 27, 28 | `/dev/i2c-3` | Compass / Barometer Sensors (TWI3) |

### C. Example Application Usage

1. **FPGA SPI Communication (AbstractX):**
   The flight control loop opens `/dev/spidev0.0` (Link A) and `/dev/spidev1.0` (Link B) as dual independent channels to exchange attitude data and motor control commands.
2. **GPS Parsing:**
   The navigation task opens `/dev/ttyS2` as a standard file descriptor at a baud rate of 9600 or 115200 to parse NMEA sentences.
3. **Compass Reading:**
   The flight sensor manager communicates with compass chips at address `0x1E` on `/dev/i2c-3`.
4. **General SPI Devices:**
   You can connect and communicate with other SPI devices (such as an secondary transceiver or payload sensor) via `/dev/spidev2.0`.

---

## 5. Software Implementation (C++ and Python Examples)

Below are functional examples demonstrating how to program the Dual-SPI interfaces and single SPI interfaces in both **C++** and **Python**.

### A. C++ Code Example

This example demonstrates how to configure and execute a full-duplex SPI transaction. For **Dual SPI** (e.g. FPGA Link A and B), you repeat this process across both `/dev/spidev0.0` and `/dev/spidev1.0` file descriptors.

```cpp
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>

class SpiBus {
private:
    int fd = -1;

public:
    SpiBus(const char* device, uint32_t speed_hz, uint8_t mode = 0, uint8_t bits = 8) {
        fd = open(device, O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open SPI device: " << device << std::endl;
            return;
        }

        // Configure SPI Mode
        if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) std::cerr << "Can't set WR mode\n";
        if (ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0) std::cerr << "Can't get RD mode\n";

        // Configure Bits per Word
        if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) std::cerr << "Can't set bits per word\n";
        if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) std::cerr << "Can't get bits per word\n";

        // Configure Max Speed (Hz)
        if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) std::cerr << "Can't set max speed\n";
        if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed_hz) < 0) std::cerr << "Can't get max speed\n";
    }

    ~SpiBus() {
        if (fd >= 0) close(fd);
    }

    bool transfer(const uint8_t* tx_buf, uint8_t* rx_buf, size_t length, uint32_t speed_hz) {
        struct spi_ioc_transfer tr;
        std::memset(&tr, 0, sizeof(tr));
        
        tr.tx_buf = (unsigned long)tx_buf;
        tr.rx_buf = (unsigned long)rx_buf;
        tr.len = length;
        tr.speed_hz = speed_hz;
        tr.bits_per_word = 8;
        tr.delay_usecs = 0;

        int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
        return (ret >= 0);
    }
};

int main() {
    // 1. Initialize Dual SPI Channels for FPGA link
    SpiBus spiLinkA("/dev/spidev0.0", 10000000); // SPI0: 10MHz (IMU data channel)
    SpiBus spiLinkB("/dev/spidev1.0", 25000000); // SPI1: 25MHz (FPGA control channel)

    // 2. Initialize General Purpose SPI Bus (e.g. for secondary transceiver)
    SpiBus spiGeneral("/dev/spidev2.0", 1000000); // SPI2: 1MHz

    // 3. Prepare data buffers
    uint8_t tx_a[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx_a[4] = {0};
    uint8_t tx_b[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t rx_b[4] = {0};

    // 4. Perform Dual-SPI transaction back-to-back
    if (spiLinkA.transfer(tx_a, rx_a, sizeof(tx_a), 10000000)) {
        std::cout << "SPI0 (Link A) Transfer Successful!" << std::endl;
    }
    if (spiLinkB.transfer(tx_b, rx_b, sizeof(tx_b), 25000000)) {
        std::cout << "SPI1 (Link B) Transfer Successful!" << std::endl;
    }

    return 0;
}
```

### B. Python Code Example

This script demonstrates how to execute transfers over the Dual-SPI and General SPI ports in Python using the `spidev` module (already packaged in this Buildroot defconfig).

```python
#!/usr/bin/env python3
import spidev
import time

def main():
    # 1. Initialize Dual SPI Link
    # spi0 maps to /dev/spidev0.0 (Bus 0, Device 0)
    spi_link_a = spidev.SpiDev()
    spi_link_a.open(0, 0)
    spi_link_a.max_speed_hz = 10000000  # 10 MHz
    spi_link_a.mode = 0

    # spi1 maps to /dev/spidev1.0 (Bus 1, Device 0)
    spi_link_b = spidev.SpiDev()
    spi_link_b.open(1, 0)
    spi_link_b.max_speed_hz = 25000000  # 25 MHz
    spi_link_b.mode = 0

    # 2. Initialize General SPI Bus
    # spi2 maps to /dev/spidev2.0 (Bus 2, Device 0)
    spi_general = spidev.SpiDev()
    spi_general.open(2, 0)
    spi_general.max_speed_hz = 1000000   # 1 MHz
    spi_general.mode = 0

    try:
        while True:
            # 3. Formulate flight control packets
            tx_data_a = [0xAA, 0xBB, 0xCC, 0xDD]
            tx_data_b = [0x11, 0x22, 0x33, 0x44]

            # 4. Perform Full-Duplex Transfers
            rx_data_a = spi_link_a.xfer2(tx_data_a)
            rx_data_b = spi_link_b.xfer2(tx_data_b)

            print(f"SPI0 Link A RX: {[hex(x) for x in rx_data_a]}")
            print(f"SPI1 Link B RX: {[hex(x) for x in rx_data_b]}")
            
            # Send general configuration packet to SPI2 device
            spi_general.xfer2([0x99, 0x88])

            time.sleep(0.1) # 10Hz test loop

    except KeyboardInterrupt:
        pass
    finally:
        spi_link_a.close()
        spi_link_b.close()
        spi_general.close()

if __name__ == "__main__":
    main()
```

---

## 6. iNAV Flight Control Integration

This system utilizes a custom **Linux port of iNAV** (hosted at [github.com/tcmichals](https://github.com/tcmichals)) running directly on the **Cubie A5E** companion computer. 

Because the target Linux distribution is built with the **real-time kernel patch (`PREEMPT_RT`)**, the iNAV flight loop runs in userspace with deterministic, high-priority real-time scheduling.

### A. Linux iNAV + FPGA I/O System Architecture

In this model, the FPGA acts as a hard real-time I/O serializer. The Linux-based iNAV core reads sensor data from the FPGA, computes the attitude correction loops, and writes the motor commands back to the FPGA over the Dual-SPI interface:

```text
       +-------------------------------------------------------+
       |                  CUBIE A5E (Linux OS)                 |
       |                                                       |
       |  [TinyML NPU Engine] <--- (Camera Video)              |
       |          |                                            |
       |          v (Guidance corrections)                     |
       |   +----------------------------+        +-----------+ |
       |   | iNAV Flight Core (RT Port) | <----> |   Wi-Fi   | |
       |   +----------------------------+        +-----------+ |
       |          | (Attitude / Motor Cmds)            |       |
       |          v                                    | (MSP) |
       |   +----------------------------+              |       |
       |   |   AbstractX SPI Driver     |              |       |
       |   +----------------------------+              |       |
       +------------------|----------------------------|-------+
                          |                            |
            Dual-SPI Link | (Sensor / DSHOT)           | Wi-Fi Connection
                          |                            | (GCS Link)
                          v                            v
       +----------------------------------------+ +-----------------+
       |           FPGA (I/O Expander)          | | Ground Control  |
       |                                        | |   Station       |
       |  +----------------------------------+  | | (iNAV Config.)  |
       |  |  AbstractX Hardware I/O Engine   |  | +-----------------+
       |  +----------------------------------+  |
       |       | (IMU)                  |       |
       |       v                        v       |
       |  [Inertial Sensors]      [DSHOT / PWM] |
       |                                |       |
       +--------------------------------|-------+
                                        v
                                 [ESCs & Motors]
```

### B. Network Telemetry Routing (TUN over Wi-Fi)

To configure, tune, and monitor the Linux-based iNAV flight controller from a PC running the **iNAV Configurator** ground control software, the virtual TUN driver routes network traffic over the Wi-Fi link:

```text
  +-----------------------------------+
  |       CUBIE A5E (Linux Board)     |
  |                                   |
  |   +---------------------------+   |
  |   | iNAV flight control core  |   |
  |   +---------------------------+   |
  |        |                          |
  |        v (MSP packets over UDP)   |
  |   +---------------------------+   |
  |   | TUN/TAP interface (tun0)  |   |
  |   +---------------------------+   |
  |        |                          |
  |        v                          |
  |   +---------------------------+   |       Wi-Fi (UDP/IP)       +----------------------+
  |   | Wi-Fi Driver (aic8800)    |   | =========================> | Ground Control PC    |
  |   +---------------------------+   |                            | (iNAV Configurator)  |
  +-----------------------------------+                            +----------------------+
```

### C. Communication Protocol Layers

* **Flight Loop Telemetry:** The Linux iNAV process opens `/dev/spidev0.0` and `/dev/spidev1.0` to exchange high-speed raw state and motor command structures with the FPGA co-processor.
* **Ground Control Link:** The virtual interface (`tun0`) encapsulates Multiwii Serial Protocol (MSP) packets into UDP/IP, transmitting them over Wi-Fi to let the iNAV Configurator connect, flash profiles, calibrate sensors, and plot live telemetry graphs.




