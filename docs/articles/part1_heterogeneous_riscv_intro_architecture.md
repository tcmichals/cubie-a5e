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
* **Sibling Generation (`sun60i` / A733)**: The **Allwinner A733** (powering the **Radxa Cubie A7A**) belongs to the newer `sun60i` big.LITTLE generation (2x Cortex-A76 + 6x Cortex-A55). While its main peripheral space is relocated, its auxiliary MCU subsystem reuses the same **XuanTie E907 RISC-V core** and adheres to the identical `remoteproc` driver model.

### Board Hardware Comparison: Radxa Cubie A5E vs. Cubie A7A

| Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **SoC** | **Allwinner T527 / A527** (`sun55i`) | **Allwinner A733** (`sun60i`) |
| **Application Cores** | 8x ARM Cortex-A55 @ 1.8 GHz | 2x ARM Cortex-A76 @ 2.0 GHz + 6x Cortex-A55 |
| **Auxiliary Real-Time Core** | **XuanTie E907 RISC-V** (RV32IMAFDC @ 200 MHz) | **XuanTie E907 RISC-V** (RV32IMAFDC @ 200 MHz) |
| **TCM Memory** | 64 KB ITCM + 64 KB DTCM | 64 KB ITCM + 64 KB DTCM |
| **On-Chip SRAM** | 320 KB System SRAM C | 320 KB System SRAM C |
| **Hardware Mailbox** | 8-channel MSGBOX (`0x03003000`) | 8-channel MSGBOX (`0x03003000`) |
| **Debug Module Access** | On-Chip DMI @ `0x07090000` (JTAG-less) | On-Chip DMI (JTAG-less) |
| **Linux Driver Model** | `sunxi_rproc.c` (`remoteproc`) | `sunxi_rproc.c` (`remoteproc`) |

---

## 2. Deriving the Physical Memory Map from the TRM

To build a reliable software foundation, we extracted the authoritative memory map from the Allwinner T527 / A523 Technical Reference Manual (TRM). On the `sun55i` architecture, the entire MCU / RISC-V subsystem resides in the physical memory window **`0x07000000 – 0x071FFFFF`**:

| Physical Address (Host ARM View) | RISC-V Core Local Address | Block / Peripheral | Description |
| :--- | :--- | :--- | :--- |
| **`0x07010000`** / `0x07102000` | `0x40010000` | **DSP / MCU CCU** | Bus clock gate (`0x20`) and Reset control (`0x100` / `0x124`) |
| **`0x07090000`** | `0x40090000` | **XuanTie Debug Module** | Hardware DM v0.13.2 / DMI register window |
| **`0x07110000`** | `0x00000000` | **ITCM (64 KB)** | Zero-wait-state Instruction TCM (Vector table / Fastcode) |
| **`0x07120000`** | `0x00080000` | **DTCM (64 KB)** | Zero-wait-state Data TCM (Stack / Heap / Data) |
| **`0x07130000`** | `0x07130000` | **System SRAM C (320 KB)** | Shared program body, data pools, and IPC memory |
| **`0x03003000`** | `0x40030000` | **Hardware MSGBOX** | 8-channel Mailbox Doorbell (ARM GIC IRQ 147 / RISC-V PLIC) |

---

## 3. On-Chip Memory-Mapped Debugging: No Physical JTAG Cables Required!

One of the most powerful features of modern heterogeneous SoCs is **Direct Memory-Mapped Debug Access (DMEM)**. 

Traditionally, debugging an auxiliary microcontroller requires:
* Soldering fine-pitch 1.27mm / 2.54mm JTAG headers to the PCB.
* Purchasing external hardware debug probes (e.g. SEGGER J-Link, T-Head CK-Link, or FTDI FT232H adapters).
* Dealing with loose jumper wires, signal integrity issues, and ground loops.

### The JTAG-less Architecture
Because the ARM Cortex-A55 Linux host and the XuanTie E907 RISC-V core share the internal high-speed system interconnect (AHB/AXI bus), the ARM host can access the **RISC-V Debug Module registers directly over MMIO at `0x07090000`**:

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
2. The TRM physical memory layout for the XuanTie E907 MCU subsystem.
3. The `dmem` memory-mapped debugging architecture and how current T527 firmware leverages Linux `remoteproc` trace buffers and hardware serial.

In **Part 2**, we will dive straight into the implementation:
* Authoring the **Linux 7.1 `sunxi_rproc.c` RemoteProc kernel driver**.
* Configuring multi-segment ELF placement (ITCM, DTCM, SRAM C) and built-in debugfs trace logging.
* Implementing low-latency IPC doorbells and shared memory communication.

---

### Series Navigation
* **Part 1: Architecture and Memory-Mapped Debugging** *(You are here)*
* **[Part 2: Building the Linux `remoteproc` Driver and Proving Hardware State](part2_building_remoteproc_and_hardware_proof.md)**
* **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**
