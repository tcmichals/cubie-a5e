# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 1): Architecture and Memory-Mapped Debugging

Heterogeneous multi-core SoCs—pairing high-performance 64-bit ARM Cortex-A application cores with low-power, deterministic auxiliary microcontrollers—have become standard in modern embedded hardware. Silicon like the **Allwinner T527 / A527** (featured on the **Radxa Cubie A5E**) integrates an octa-core ARM Cortex-A55 cluster alongside an auxiliary **XuanTie E907 RISC-V core** (RV32IMAFDC @ 200 MHz) and a **Cadence Tensilica HiFi4 Audio DSP** (@ 600 MHz).

Getting these co-processors online requires establishing reliable hardware lifecycle control, clock tree synchronization, and deterministic memory placement before loading production firmware.

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

This article is **Part 1 of a series** documenting the practical bring-up of the XuanTie E907 RISC-V co-processor on Linux:
* **Part 1 (This Article)**: Architecture, TRM memory maps, the RemoteProc driver model, and on-chip memory-mapped debugging.
* **Part 2**: Authoring the Linux `remoteproc` kernel driver, multi-segment ELF placement, and proving hardware state with automated Python DMI scripts.
* **Part 3**: General embedded firmware development, TCM vs DRAM memory determinism, lightweight lock-free IPC (libmetal), live GDB workflows, and an introduction to bare-metal C++ coroutines.
* **Part 4**: Deep dive into C++20 coroutines on bare-metal RISC-V—benchmarks, memory profiles, and comparison against RTOS task switching.

---

## 1. A Note on Silicon Naming: `T527` vs `A527` vs `sun55i-a523`

When navigating Allwinner documentation and Linux kernel sources, the naming taxonomy can be confusing:

```text
                               ┌─────────────► Allwinner T527 (Industrial SBC — Radxa Cubie A5E)
                               │
sun55i Generation (Same Die IP) ┼─────────────► Allwinner A527 (Commercial SBC)
                               │
                               └─────────────► Allwinner A523 (Tablet / OTT Platform)
```

* **Same Silicon Core**: The **T527** (industrial grade) and **A527** (commercial grade) share the exact same internal silicon die, bus topology, and MCU memory map as the **A523**.
* **Kernel Codename (`sun55i`)**: In upstream Linux and U-Boot, this generation is codenamed **`sun55i`**. The board device tree (`sun55i-a527-cubie-a5e.dts`) includes the base `sun55i-a523.dtsi`, and the clock driver is `ccu-sun55i-a523-mcu.c`.
* **Sibling Generation (`sun60i` / A733)**: The **Allwinner A733** (powering the **Radxa Cubie A7A**) belongs to the newer `sun60i` big.LITTLE generation (2x Cortex-A76 + 6x Cortex-A55). While its main peripheral space is relocated, its auxiliary MCU subsystem reuses a **XuanTie RISC-V core** (E902) executing out of SRAM A2 and adheres to the identical `remoteproc` driver model.

### Board Hardware Comparison: Radxa Cubie A5E vs. Cubie A7A

| Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **SoC** | **Allwinner T527 / A527** (`sun55i`) | **Allwinner A733** (`sun60i`) |
| **Application Cores** | 8x ARM Cortex-A55 @ 1.8 GHz | 2x ARM Cortex-A76 @ 2.0 GHz + 6x Cortex-A55 |
| **Auxiliary Real-Time Core** | **XuanTie E907 RISC-V** (RV32IMAFDC @ 200 MHz) | **XuanTie E902 RISC-V** (RV32EMC @ 200 MHz) |
| **Fast On-Chip SRAM** | 128 KB Shared PubSRAM C (`0x00020000`) + 256 KB Dedicated MCU SRAM (`0x3FFC0000` Core / `0x07280000` Host) | 208 KB Shared SRAM A2 (`0x00040000`) |
| **Boot & Control Registers** | 4 KB E907 CFG (`0x07130000`, `STA_ADD_REG` @ `0x0204`) | PRCM `r_ccu` Reset Control (Hardwired Reset Vector @ `0x00040000`) |
| **Hardware Mailbox** | 8-channel MSGBOX (`0x03003000`) | 8-channel MSGBOX (`0x03003000`) |
| **Debug Module Access** | On-Chip DMI @ `0x07090000` (JTAG-less) | On-Chip DMI (JTAG-less) |
| **Linux Driver Model** | `sunxi_rproc.c` (`remoteproc`) | `sunxi_rproc.c` (`remoteproc`) |

---

## 2. Deriving the Physical Memory Map from Silicon and Device Tree

Heterogeneous firmware execution requires an exact understanding of memory address spaces. On Allwinner SoCs, the ARM64 host application cores and the XuanTie E907 RISC-V core access shared SRAM and peripherals through the system interconnect, but certain memory windows undergo address translation between the host interconnect and the core-local bus.

### Verified Hardware Memory Windows (Allwinner T527 / A523)

The authoritative memory mapping verified on silicon and registered in the Linux RemoteProc driver (`sunxi_rproc.c` / `sun55i-a523.dtsi`) is structured as follows:

| Memory Region | Host ARM Physical Address | RISC-V E907 Core Address | Size | RemoteProc DTS Binding | Description & Hardware Role |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Shared PubSRAM C** | **`0x00020000`** | **`0x00020000`** | **128 KB** | `reg-names = "sram"` | Primary boot and execution window (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`). 1:1 identity mapped on both interconnects. |
| **Dedicated MCU SRAM (`r_sram`)** | **`0x07280000`** | **`0x3FFC0000`** | **256 KB** | `reg-names = "r_sram"` | Zero-wait-state dedicated MCU SRAM space 0. High-performance memory for real-time control loops and high-throughput SPSC ring buffers. |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | `reg-names = "cfg"` | Hardware MMIO registers: Version (`0x0000`), Boot Entry Vector `STA_ADD_REG` (`0x0204`), and Work Mode / Lockup Status `WORK_MODE_REG` (`0x0248`). Not general SRAM. |
| **MCU CCU Clocks & Resets** | **`0x07102000`** / `0x07010000` | **`0x07102000`** / `0x07010000` | **64 KB** | `clocks = <&mcu_ccu ...>` | Clock gate (`0x07102120`), resets (`0x07102124`: bit 16 CFG, bit 17 DBG, bit 18 CORE), and PubSRAM clock/reset (`0x07102114`). |
| **Hardware MSGBOX** | **`0x03003000`** | **`0x03003000`** | **4 KB** | `mboxes = <&msgbox 0>, <&msgbox 1>` | 8-channel bi-directional doorbell FIFO. Channel 0: RISC-V to Linux (GIC SPI 147); Channel 1: Linux to RISC-V (PLIC IRQ 25). |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | `memory-region` (carveout) | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`). |
| **DDR DMA Payload Pool** | **`0x48100000`** | **`0x48100000`** | **1 MB** | `memory-region` (carveout) | Non-cacheable DDR DMA payload buffer pool for high-bandwidth IPC transfers. |
| **Main Peripheral Space** | **`0x02000000`+** | **`0x02000000`+** | — | Native SoC buses | 1:1 mapped peripherals: PIO GPIO controller (`0x02000000`), UART0 debug console (`0x02500000`), UART2 navigation port (`0x02500800`), SPI0 (`0x04025000`). |

### Visual Address Translation Architecture

```text
+===================================================================================+
|                  ALLWINNER T527 / A523 MEMORY MAPPING ARCHITECTURE                |
+===================================================================================+

  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x00020000 - 0x0003FFFF [ 128 KB ] ─────────────> 0x00020000 - 0x0003FFFF (PubSRAM C)
    (Device Tree: "sram")                             (Default Boot, .vectors, .text, stack)

  0x07280000 - 0x072BFFFF [ 256 KB ] ─────────────> 0x3FFC0000 - 0x3FFFFFFF (Dedicated SRAM)
    (Device Tree: "r_sram")                           (Zero-Wait-State High-SRAM Window)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (CFG Regs)
    (Device Tree: "cfg", STA_ADD_REG 0x204)           (Boot Vector & Lockup Status)

  0x03003000 - 0x03003FFF [   4 KB ] ─────────────> 0x03003000 - 0x03003FFF (MSGBOX)
    (ARM GIC IRQ 147)                                 (RISC-V PLIC IRQ 25)

  0x48000000 - 0x48000FFF [   4 KB ] ─────────────> 0x48000000 - 0x48000FFF (trace0)
    (RemoteProc Trace Carveout)                       (Debugfs Live Logging)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR DMA Pool)
    (Reserved Memory Pool)                            (High-Bandwidth Telemetry Buffers)
+===================================================================================+
```

### Silicon Reality: Clarifying Theoretical TCM vs. Operational SRAM

Early Allwinner documentation and legacy references from older SoCs (such as the Allwinner D1) placed theoretical ITCM at `0x00000000` (host physical `0x07110000`) and DTCM at `0x00080000` (host physical `0x07120000`).

On actual Allwinner A523/T527 silicon:
1. **No Operable 0x0 TCM Window**: `0x07110000` and `0x07120000` are reserved register spaces. Setting `STA_ADD_REG` (`0x07130204`) to `0x00000000` causes an immediate bus error on instruction fetch, triggering a double-fault on `mtvec` and forcing the XuanTie core into **Hardware Lockup** (`WORK_MODE_REG 0x07130248` Bit 3 `BIT_LOCK_STA = 1`).
2. **Operational Execution Windows**: The core boots cleanly without lockup when `STA_ADD_REG` points to either **`0x00020000`** (Shared PubSRAM C) or **`0x3FFC0000`** (Dedicated MCU SRAM), running with `BIT_LOCK_STA = 0` (`WORK_MODE_REG = 0x00000003`).
3. **Firmware Layout**: Production firmware links `.vectors`, `.text`, `.rodata`, `.data`, `.bss`, and the stack into **PubSRAM C (`0x00020000`, 128 KB)**, while high-performance execution loops and SPSC ring buffers use **Dedicated MCU SRAM (`0x3FFC0000`, 256 KB)**.

---

## 3. On-Chip Memory-Mapped Debugging: No Physical JTAG Cables Required!

One of the most powerful features of modern heterogeneous SoCs is **Direct Memory-Mapped Debug Access (DMEM)**. 

Traditionally, debugging an auxiliary microcontroller requires:
* Soldering fine-pitch 1.27mm / 2.54mm JTAG headers to the PCB.
* Purchasing external hardware debug probes (e.g. SEGGER J-Link, T-Head CK-Link, or FTDI FT232H adapters).
* Dealing with loose jumper wires, signal integrity issues, and ground loops.

### The JTAG-less Architecture
In heterogeneous SoCs supporting direct debug interconnect routing, the host application processor can access the auxiliary Debug Module registers over MMIO (allocated at `0x07090000` in the T527 TRM):

```text
┌─────────────────────────────────────────────────────────────┐
│                    GDB Debugger Host                        │
│   (riscv-none-elf-gdb / gdb set arch riscv:rv32)            │
└─────────────────────────────┬───────────────────────────────┘
                              │ GDB Remote Serial Protocol (RSP) :3333
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                 OpenOCD (Running on Target)                 │
│  - Translates GDB commands into RISC-V Debug Module actions │
│  - Listens on TCP port 4444 for Telnet / Python automation  │
└─────────────────────────────┬───────────────────────────────┘
                              │ Direct MMIO / remote_bitbang
                              ▼
┌─────────────────────────────────────────────────────────────┐
│          On-Chip XuanTie E907 Debug Module (DM v0.13.2)     │
│                 (Physical Address 0x07090000)               │
└─────────────────────────────┴───────────────────────────────┘
```

> **Precedent in the Open-Source Community (TI, ST, and BeagleBoard):**  
> This on-chip debugging methodology has a proven lineage in the open-source Linux community. **Nishanth Menon** (Texas Instruments) and **Jason Kridner** (BeagleBoard.org Foundation) pioneered self-hosted "soft-wire" JTAG-less debugging on platforms like the **BeaglePlay** and **BeagleBone AI-64** (TI AM62x / AM64x) by introducing the `dmem` driver into OpenOCD (`board/ti_am625_swd_native.cfg`). By mapping debug registers directly across the internal bus via direct MMIO, developers could debug auxiliary cores natively from Linux without external hardware probes or header soldering. STMicroelectronics and NXP implement similar memory-mapped debug interfaces on their heterogeneous SoCs.

The presentation that inspired this approach is well worth watching:  
🎥 **[Debugging Heterogeneous SoC Using OpenOCD — Nishanth Menon, Texas Instruments (YouTube)](https://youtu.be/hKFvxgbHUfg?si=Mhd7lEJgq9oBp3t9)**

> **Hardware Architecture & Future Outlook:**  
> While memory-mapped debug access (`dmem`) is supported on TI (AM62x/K3) and STMicroelectronics (STM32MP1) SoCs, current **Allwinner T527 silicon does not route a `dmem` bus interface** for the RISC-V Debug Module to the non-secure ARM interconnect.
> 
> We hope Allwinner will incorporate a memory-mapped `dmem` interface in future silicon revisions so that developers can take full advantage of native Linux-hosted OpenOCD and GDB remote debugging. On current T527 hardware, production debugging is achieved cleanly through Linux RemoteProc trace buffers (`trace0`), dedicated serial UART logging (`S_UART0`), lock-free shared SRAM ring buffers, and external JTAG hardware probes.

---

## 4. Summary & What's Next in Part 2

In this introductory article, we established:
1. The silicon naming relationship between **T527**, **A527**, and **`sun55i-a523`**.
2. The verified physical memory layout and RemoteProc bindings for the XuanTie E907 MCU subsystem.
3. The `dmem` memory-mapped debugging architecture and how current T527 firmware leverages Linux `remoteproc` trace buffers and hardware serial.

In **Part 2**, we will dive straight into the implementation:
* Authoring the **Linux 7.1 `sunxi_rproc.c` RemoteProc kernel driver**.
* Configuring multi-segment ELF placement (PubSRAM C, Dedicated MCU SRAM, and DDR carveouts) and built-in debugfs trace logging.
* Implementing low-latency IPC doorbells and shared memory communication.

---

### Series Navigation
* **Part 1: Architecture and Memory-Mapped Debugging** *(You are here)*
* **[Part 2: Building the Linux `remoteproc` Driver and Proving Hardware State](part2_building_remoteproc_and_hardware_proof.md)**
* **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**
