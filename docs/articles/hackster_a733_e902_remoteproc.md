# Turning an 8-Core Desktop Processor into a Hard Real-Time Robotics & Industrial Engine: An Industrial Hacker's Guide to Unlocking XuanTie RISC-V Cores

**By Tim Michals**  
*Featured for Hackaday & Open-Source Industrial Automation Hackers*

---

## 🚀 The Hack: Why Bolt an STM32 to a Desktop SBC When There's a 200 MHz RISC-V Core Already on the Die?

Every builder of robotics, autonomous agricultural machinery, CNC machine controls, and flight systems knows the classic architecture compromise: you want desktop-class application compute (quad Cortex-A76 cores, 6 GB LPDDR5, MIPI CSI cameras, hardware video encoding, and ROS 2 / Linux networking), but your physical hardware I/O demands **hard real-time determinism with zero jitter**:
- **Industrial Robotics & AMRs**: ROS 2 Nav2 and 3D SLAM on the host $\leftrightarrow$ Sub-microsecond Field Oriented Control (FOC) motor commutation, wheel encoder tracking, and safety interlocks on the micro.
- **Precision Agriculture**: High-framerate camera vision and edge NPU weed-detection inference $\leftrightarrow$ Microsecond-accurate solenoid spray valve actuation synced to GPS and ground speed as the tractor sweeps across the field.
- **Factory Automation & Machine Control**: G-code parsing, EtherCAT master communication, and web HMIs $\leftrightarrow$ Microsecond-exact multi-axis step/direction pulse trains without a single missed cycle.
- **Autonomous Drones & Flight Controllers**: Optical flow, companion vision, and trajectory generation $\leftrightarrow$ 1 kHz flight stabilization loops and DShot ESC telemetry.

So what does the industry do? We bolt a secondary microcontroller—an STM32, RP2040, or Teensy—next to our single-board computer. We wire up an umbilical cord over UART, SPI, or USB, and spend the next six months debugging level shifters, baud-rate bottlenecks, packet framing errors, and ground loops that fail under harsh vibration.

Here is the dirty secret hiding inside modern application processors: **the companion microcontroller is already on the silicon.**

On the **Allwinner A733** (an octa-core 4× Cortex-A76 + 4× Cortex-A55 SoC powering boards like the **Radxa Cubie A7A**) and its sibling **Allwinner T527** (powering the **Radxa Cubie A5E**), the silicon vendor baked a dedicated **32-bit T-Head XuanTie RISC-V coprocessor (E902 / E906 @ 200 MHz)** right onto the system bus. It has single-cycle access to dedicated on-chip SRAM, hardware mailbox doorbell FIFOs, and direct memory-mapped access to the SoC's timers, PWM, SPI, and GPIO.

So why isn't everyone using it? Because in vendor land, this capable 200 MHz RISC-V core is trapped in a glorified sleep-switch role: the **System Control Processor (SCP / ARISC)**. Its sole vendor job is to wait for the Linux kernel to go into deep sleep, toggle PMIC voltage rails, and wake the board back up. When the host Linux OS runs actively, the RISC-V core sits completely idle.

As industrial hackers building hard real-time systems, we refused to let 200 MHz of bare-metal RISC-V silicon go to waste. 

This is the unfiltered engineering story of how we hacked the Allwinner bootchain, freed the XuanTie core from vendor sleep jail, avoided all the landmines, and built a mainline Linux `remoteproc` / `rpmsg` pipeline that transforms an 8-core desktop SBC into a self-contained, dual-personality industrial supercomputer:
1. **The Silicon Architecture**: How an octa-core 64-bit Arm cluster shares fabric with a 32-bit RISC-V engine.
2. **The Autopsy of Mistakes**: Chasing the Arm TrustZone (EL3) phantom, reverse-engineering BL31 binaries, and finding the real clock/reset vector latching race.
3. **The 16 KB Reserved SRAM Trap**: The chapter 3 footnote that silently ate our instruction fetches.
4. **The Breakthrough**: How removing `scp.fex` from the boot package unlocked the **stock Arm Trusted Firmware** with zero binary hacks.
5. **Mainline Linux Driver Design**: Upstream RemoteProc and VirtIO RPMsg over shared SRAM with sub-15 µs round-trip latency.

---

## 🏗️ Hardware Architecture & Memory Topology

Before touching code, let's look at how the silicon is laid out.

![Allwinner A733 Heterogeneous System Architecture](media/a733_e902_system_topology.svg)

### The Silicon Taxonomy: Desktop Power Meets Embedded RISC-V

We tested this architecture across both members of Allwinner's modern heterogeneous family:

| Silicon Specification | Radxa Cubie A7A (Allwinner A733) | Radxa Cubie A5E (Allwinner T527 / A527) |
| :--- | :--- | :--- |
| **Host Application Cluster** | **4× Cortex-A76 @ 2.0 GHz + 4× Cortex-A55 @ 1.8 GHz** (True Desktop-Class Compute) | 8× Cortex-A55 @ 1.8 GHz (Power-Efficient Cluster) |
| **System Memory** | Up to **6 GiB LPDDR5** (High-Bandwidth Interconnect) | Up to 4 GiB LPDDR4X |
| **Real-Time Coprocessor** | **T-Head XuanTie E902** (RV32EMC, 16 GPRs `x0`–`x15`, 200 MHz) | **T-Head XuanTie E906** (RV32IMAFCX, 32 GPRs, FPU & DSP, 200 MHz) |
| **On-Chip Real-Time Memory** | System SRAM A2 (208 KB Total, **192 KB Usable Coprocessor Pool**) | SRAM A3 (32 KB) + Cadence HiFi4 DSP RAM (64 KB) + Dedicated TCM |
| **Hardware Signaling** | Hardware Mailbox (`msgbox0` / `0x07090000`, 8 bidirectional FIFOs) | Hardware Mailbox (`msgbox0` / `0x03004000`, 8 bidirectional FIFOs) |
| **Vision & Hardware Media** | 4K MIPI CSI-2 + **Cedrus Hardware VPU (1080p78 H.264 / H.265)** | MIPI CSI-2 + Hardware Video Engine |
| **Target Application Roles** | **Linux**: Computer vision, SLAM, ROS 2, smart ag weed detection, web HMIs $\leftrightarrow$ **RISC-V**: Sub-microsecond motor FOC, DShot, multi-axis CNC pulse generation, precision spray valve timing, fail-safe e-stops | **Linux**: Fleet telemetry, supervisory control, cloud sync $\leftrightarrow$ **RISC-V**: Sensor fusion, fast ADC sampling, fieldbus gateway (CAN/Modbus), DSP filtering |

Both chips share the same fundamental paradigm: the host runs mainline Linux 7.1 `PREEMPT_RT` on the Arm cores, while the auxiliary XuanTie RISC-V core takes direct ownership of real-time peripherals without touching the host OS.

---

## ⚠️ The Mistakes We Made: Autopsy of False Assumptions

Bring-up of undocumented vendor silicon is rarely a straight line. Here is an honest accounting of our dead ends, false assumptions, and how silicon readbacks proved our early hypotheses wrong.

### Mistake 1: Blaming Arm TrustZone & Writing a Binary Trampoline
In vendor Allwinner documentation, peripheral security is managed by the **Secure Peripheral Controller (SPC)**. When our early attempts to write to the E902 reset vector configuration register (`E902_STA_ADD_REG` at `0x07032204`) resulted in our written addresses disappearing, we jumped to a familiar conclusion:

> *"Arm TrustZone (EL3) is blocking non-secure Linux access to the coprocessor configuration registers!"*

We spent days reverse-engineering the vendor Arm Trusted Firmware (`bl31.bin`). We identified `R_SPC` Port 3 (`0x07002034`), disassembled the binary, and wrote a Python patching tool (`patch_bl31_security.py`) that injected an in-segment trampoline at VA `0x480131c8` to force-write `0xFFFFFFFF` into the SPC gate.

**The Reality Check:**
When we finally ran live readbacks on the board, we discovered something humbling:
1. `E902_STA_ADD_REG` reads from non-secure Linux **never triggered an SError abort**, with or without the patch!
2. Writing to `E902_STA_ADD_REG` while holding the E902 in reset silently ignored the write, even in EL3 secure mode!
3. The stock, unpatched BL31 shipped by Radxa **never writes to `R_SPC` Port 3 at all**. Its hardware reset default is already open to non-secure access!

The binary patch was a phantom fix. The real bug was in hardware reset sequencing.

---

### Mistake 2: The Reset Vector Latching Race
Why did writes to `0x07032204` vanish?

In standard Allwinner cores (like the E906 on the A527), writing the firmware entry address before deasserting reset works because the configuration block registers are clocked independently.

On the A733 E902, the silicon has a critical quirk:
```
The E902_CFG peripheral block ignores ALL bus writes while RISCV_CFG_RST is held in reset!
```

In our first Linux driver implementation, `sunxi_rproc_start()` executed:
```c
/* WRONG INITIAL ATTEMPT */
writel(start_address, priv->cfg_base + E902_STA_ADD_REG); // SILENTLY DROPPED!
reset_control_deassert(priv->rst_cfg);                   // Now clock starts...
reset_control_deassert(priv->rst_core);                  // Core boots from garbage!
```
Because `rst_cfg` was asserted during the `writel`, the register hardware dropped the write on the floor. When the core was released, it jumped to its internal power-on default: `0x40014000` (an address pointing into zero-filled DRAM). The Linux sysfs node reported `state: running`, but the core was executing NOP sleds in empty RAM!

**The Working Sequence:**
By analyzing the assembly of the vendor bootloader immediately after `[SCP] :set arisc reset to de-assert state`, we uncovered the strict, mandatory register sequence:

```
+-------------------------------------------------------------------------+
|                  Correct E902 Silicon Startup Sequence                  |
+-------------------------------------------------------------------------+
                                     |
                                     v
                 +---------------------------------------+
                 | 1. Enable R_CCU bus & cfg clock gates |
                 |    (0x0701021C |= BIT(0) | BIT(1))    |
                 +-------------------+-------------------+
                                     |
                                     v
                 +---------------------------------------+
                 | 2. Release RST_BUS_RISCV_CFG          |
                 |    (0x0701021C |= BIT(16))            |
                 +-------------------+-------------------+
                                     |
                                     v
                 +---------------------------------------+
                 | 3. Write E902_STA_ADD_REG (0x07032204)|
                 |    Address STICKS now that CFG is on! |
                 +-------------------+-------------------+
                                     |
                                     v
                 +---------------------------------------+
                 | 4. Release E902 core reset            |
                 |    (R_CPUCFG 0x07050008 |= BIT(0))    |
                 +-------------------+-------------------+
                                     |
                                     v
                 +---------------------------------------+
                 | 5. Core fetches directly from valid   |
                 |    entry point in SRAM A2!            |
                 +---------------------------------------+
```

---

### Mistake 3: The "Forbidden" First 16 KB of SRAM A2
The Allwinner A733 User Manual lists **System SRAM A2** spanning `0x00040000` to `0x00073FFF` (208 KB).

Naturally, we linked our initial RISC-V ELF starting right at `0x00040000`. And once again, the core refused to execute.

Buried deep in chapter 3 of the hardware user manual is a single sentence:
> *"SRAM A2: 16K + 192K (The 16K is only used by CPUS.)"*

The first 16 KB (`0x00040000`–`0x00043FFF`) is hardwired exclusively for power-management state machines and ignores CPU instruction fetch requests from the E902. The actual usable general-purpose coprocessor SRAM begins strictly at **`0x00044000` (host view) / `0x40004000` (E902 view)**, providing **192 KB** of contiguous execution memory.

Once we adjusted the linker script (`e902_sram.ld`) to originate at `0x40004000`, the core immediately sprang to life.

---

## 💡 The Breakthrough: Removing `scp.fex` from the Boot Stream

The standard Allwinner bootchain packages multiple binary components into a single bootloader image (`radxa_a733_bootloader.bin` or `toc1.fex`):
1. **BL2 / U-Boot SPL**: Initializes DRAM.
2. **BL31 (Arm Trusted Firmware)**: Initializes EL3 secure monitor and PSCI.
3. **SCP Firmware (`scp.fex`)**: The vendor E902 binary.
4. **BL33 (U-Boot proper)**: Application bootloader.

![Boot Sequence Autopsy: Vendor Firmware vs. Clean Remoteproc](media/scp_fex_bootflow_autopsy.svg)

When BL31 runs, it checks whether `scp.fex` was loaded by SPL. If found, BL31:
- Copies `scp.fex` into SRAM A2.
- Boots the E902 into ARISC power-management mode.
- Locks the E902 into handling suspend/resume events.

**The Solution:**
By stripping `scp.fex` from the TOC1 package entirely:
1. BL31 initializes normally and provides standard PSCI power services to Linux.
2. BL31 detects no SCP payload and **completely skips touching the E902**.
3. The E902 and its clocks, resets, and SRAM remain in their clean, uninitialized power-on state.
4. When Linux 7.1 boots, the mainline `sunxi_rproc` kernel driver takes 100% control of the E902 as a dynamic real-time coprocessor!

**Zero binary patches to Arm Trusted Firmware required.** Any standard Linux distribution can boot with this configuration.

---

## 🛠️ Software Architecture: The Linux `remoteproc` & `rpmsg` Subsystem

With the hardware behaving cleanly, we implemented the software stack to Linux Foundation upstream standards.

![Linux RemoteProc & VirtIO RPMsg Architecture](media/remoteproc_ipc_flow.svg)

### The Driver Architecture (`sunxi_rproc.c`)
The driver integrates with the standard Linux RemoteProc framework:
- **Resource Table Extraction**: Automatically parses ELF sections for `.resource_table` to establish VirtIO rings and memory carveouts.
- **Clock & Reset Management**: Uses standard device-tree clocks (`devm_clk_get()`) and resets (`devm_reset_control_get()`).
- **Memory Address Translation (ATT)**: Maps the host Arm physical addresses to the E902 internal bus view. Host `0x00044000` maps 1:1 to E902 `0x40004000`.
- **Debugfs Telemetry**: Exposes `/sys/kernel/debug/remoteproc/remoteproc0/trace0`, giving host userspace a real-time, non-blocking window into E902 printk buffers.

---

## ⚡ Real-Time Benchmarks: Linux Host to RISC-V IPC

To test performance, we built a standardized, apples-to-apples ping-pong round-trip benchmark:
- 1,000 round-trip messages of 512 bytes each.
- Full 4-byte sequence tag and checksum verification on every packet.
- Tested across three different IPC architectures.

| Benchmark Profile | Transport Mechanism | Avg Round-Trip Time (RTT) | Message Rate | Data Integrity |
| :--- | :--- | :---: | :---: | :---: |
| **Direct SRAM SPSC (C++)** | Lock-free Single-Producer Single-Consumer queue in SRAM A2 | **14.47 µs** | **63,139 msgs/sec** | 100% (0 errors) |
| **Linux UIO + libmetal (C++)** | Direct userspace register access + hardware mailbox | **14.55 µs** | **60,498 msgs/sec** | 100% (0 errors) |
| **VirtIO RPMsg (C++)** | Linux standard `/dev/rpmsg0` character device | **120.50 µs** | **8,298 msgs/sec** | 100% (0 errors) |
| **VirtIO RPMsg (Python)** | Python standard `os.read()` / `os.write()` on `/dev/rpmsg0` | **126.54 µs** | **5,982 msgs/sec** | 100% (0 errors) |

### Key Takeaway for Developers:
If you need strict microsecond-range determinism (e.g. high-frequency IMU reads or motor commutation), the **Direct SRAM SPSC** profile achieves **14.47 microseconds** average latency.
If you need seamless integration into standard Linux software frameworks (like ROS 2 nodes or Python daemons), **VirtIO RPMsg** provides a battle-tested POSIX API with **120 microsecond** round-trip speeds.

---

## 🎓 The Industrial Hacker's Takeaway: Stop Bolting Companion MCUs to SBCs

By looking past vendor BSPs and treating modern application processors as heterogeneous playgrounds, we eliminated an entire physical subsystem across robotics, smart agriculture, and industrial machine control:
- **Zero Companion Microcontrollers**: No external STM32, RP2040, or Teensy board taking up enclosure volume, BOM cost, and payload weight.
- **Zero Umbilical Cable Failures**: No UART, SPI, or USB wiring harness to vibrate loose on an autonomous tractor, pick up motor EMI on an industrial robot, or drop packets in a high-vibration machine.
- **Microsecond IPC Speed**: Sub-15 µs communication round-trips over on-chip shared SRAM, faster than any external SPI bus could ever dream of.
- **Clean Linux Upstreaming**: All of this runs on standard mainline Linux 7.1 with standard `remoteproc` and `rpmsg` interfaces, with zero binary blobs and zero hacks to Arm Trusted Firmware.

The next time you look at a modern desktop-class SBC, don't just see an application processor. Dig into the memory map, probe the unclocked configuration registers, and see what dormant silicon is waiting to be freed.

---

## 🔮 What's Next: Hacking Through the Camera, H.265 & the NPU

Freeing the RISC-V coprocessor was only phase one. Right now, our lab bench is active on the next frontiers:
- **Zero-Copy Camera & H.265 Video Encoding**: We have already brought up the 2-lane MIPI CSI-2 camera receiver, authenticated the undocumented Hardware ISP 602 on silicon, and proven a zero-copy DMA-buf pipeline passing 1080p frames directly into the Cedrus hardware video engine for H.264 encoding at 78 fps with < 1% CPU utilization. Next in the crosshairs: unlocking **hardware H.265 (HEVC)** encoding!
- **Freeing the Onboard 3 TOPS NPU**: Rather than accepting proprietary vendor binary blobs for computer vision, our upcoming milestone brings up the onboard VeriSilicon VIP9000 NPU using the mainline Linux `etnaviv` DRM driver and open-source Mesa Teflon TFLite delegate—delivering an entirely open edge AI vision stack from photon to motor pulse.

---

### Source Code, Kernel Trees & Schematics
- **Mainline Linux Kernel Tree**: [linux-cubie on GitHub](https://github.com/tcmichals/linux-cubie) (branch `a7a_remoteproc_camera_usb`)
- **Firmware, DTBs & Tooling**: [cubie-a5e on GitHub](https://github.com/tcmichals/cubie-a5e)
- **Upstream Linux Remoteproc RFC Submissions**: [Kernel Lore Mailing List Archive](https://lore.kernel.org/linux-sunxi/20261101185735.762559-1-tcmichals@gmail.com/)
