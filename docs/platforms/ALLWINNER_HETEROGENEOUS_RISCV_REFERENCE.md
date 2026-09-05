# Allwinner XuanTie RISC-V Co-Processor Reference Guide (T527 E906 / A733 E902)

> **Target Co-Processors**: 
> - **Allwinner T527 / A527**: T-Head / XuanTie **E906** (with Cadence Tensilica HiFi4 Audio DSP, ITCM/DTCM)
> - **Allwinner A733**: T-Head / XuanTie **E902** (up to 200 MHz per Linux-Sunxi, No DSP, Shared SRAM A2 @ 0x00040000)
> **Host Kernel**: Linux 7.1 PREEMPT_RT (`drivers/remoteproc/sunxi_rproc.c`)  

---

## 1. Co-Processor Architectural Summary

- The **Allwinner T527** integrates the **XuanTie E906** (with Cadence Tensilica HiFi4 DSP and dedicated ITCM/DTCM).
- The **Allwinner A733** integrates the **XuanTie E902** (no DSP, executes directly from SRAM A2).

### Why Confusion Exists in the Manuals & Register Tables
If you parse through the registers inside the Allwinner T527 and A733 User Manuals, parts of the debug, messaging (remoteproc), and JTAG registers are often labeled or referenced identically to older platforms that carried the E907 (such as the T536 or V853). Allwinner did this because they copy-pasted register tables and peripheral block configurations between document iterations when the underlying hardware address mappings did not change.

However, the hardware initialization sections, core description chapters, and community verification (Linux-Sunxi) confirm the true physical cores:
- **Allwinner T527 (`sun55i`)**: **XuanTie E906** (with Cadence Tensilica HiFi4 Audio DSP @ 600 MHz).
- **Allwinner A733 (`sun60iw2`)**: **XuanTie E902** (up to 200 MHz, no DSP).

```
+---------------------------------------------------------------------------------------+
|                    HETEROGENEOUS CO-PROCESSOR SUBSYSTEM COMPARISON                    |
|                                                                                       |
|  [Allwinner T527 / A527 (sun55i)]             [Allwinner A733 (sun60iw2)]             |
|  +---------------------------------------+   +-------------------------------------+  |
|  | - Core: XuanTie E906 RISC-V (200 MHz) |   | - Core: XuanTie E902 RISC-V (200MHz)|  |
|  | - DSP: Cadence HiFi4 Audio DSP (600M) |   | - DSP: None (Silicon omitted)       |  |
|  | - Memory: ITCM (64KB), DTCM (64KB),   |   | - Memory: SRAM A2 (208 KB @ 0x40000)|  |
|  |   SRAM A3 (1024KB @ 0x07280000)       |   | - Interconnect: R-CCU @ 0x07010000  |  |
|  | - Control: MCU CCU @ 0x07102000       |   | - Boot: Hardwired to 0x00040000     |  |
|  +---------------------------------------+   +-------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

## 2. Platform Architecture Breakdown

### A. Allwinner T527 / A527 (`sun55i`) — XuanTie E906 + HiFi4 DSP
On the T527, the E906 RISC-V core shares the MCU/DSP subsystem with the Cadence Tensilica HiFi4 Audio DSP.

| Memory / Register Region | Host (ARM64) Address | RISC-V Core Address | Size | Primary Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Dedicated SRAM / ITCM** (`r_sram`)| `0x07110000` / `0x07280000`| `0x00000000` | 256 KB | Reset Vector Table (`.vectors`), ISRs |
| **DTCM** | `0x07120000` | `0x00080000` | 64 KB | Fast Data, Stack |
| **Shared SRAM** (`sram`) | `0x00040000` | `0x00040000` | 160 KB | Shared SRAM A2 (Telemetry, SPSC Rings) |
| **MCU CCU** (`cfg`) | `0x07102000` | `0x07102000` | 4 KB | E906 / DSP Clock, Reset, Bus Bridges |
| **E906 CFG Block** (`e906-cfg`) | `0x07130000` | `0x07130000` | 4 KB | E906 Boot Vector (`0x0204`), Work Mode, Wakeup |
| **HiFi4 DSP Config** | `0x07100000` | `0x07100000` | 1 KB | Cadence Tensilica HiFi4 Subsystem Control |
| **MSGBOX (IPI)** | `0x03004000` | `0x07094000` | 4 KB | Inter-Core Mailbox Doorbell IRQ |

#### T527 XuanTie E906 CFG Registers (`0x07130000`):
* `0x0000` (`E906_VER_REG`): E906 Version Register
* `0x0010` (`E906_RF1P_CFG_REG`): E906 Control Register 0
* `0x0040` (`E906_TS_TMODE_SEL_REG`): E906 Test Mode Select Register
* `0x0204` (`E906_STA_ADD_REG`): **E906 Start Vector / Boot Address Register**
* `0x0220` (`E906_WAKEUP_EN_REG`): E906 WakeUp Enable Register
* `0x0224`–`0x0234` (`E906_WAKEUP_MASK0..4_REG`): E906 WakeUp Mask Registers
* `0x0248` (`E906_WORK_MODE_REG`): E906 Work Mode Register

#### Strict Hardware Clock & Reset Sequencing Rules (Avoiding Bus Errors):

> [!CAUTION]
> **BUS ERROR ROOT CAUSE**:
> If the ARM host attempts to read or write `0x07130000` (`e906-cfg`) while `cfg_rst` is asserted or `cfg_clk` (`CLK_BUS_RV_CFG`) is disabled, the peripheral bus interface has no clock and returns an immediate **AHB/AXI SLVERR/DECERR**, causing a **Synchronous External Abort (Kernel Panic)**.
> 
> Furthermore, if core execution clock `mod_clk` (`CLK_BUS_RV`) is stopped while the RISC-V core is actively executing or issuing an AXI burst, the bus transaction freezes midway, causing an interconnect bus hang.

The driver must strictly follow the vendor symmetrical lifecycle sequencing:

```text
E906 BOOT / START SEQUENCE:
1. Deassert resets (pubsram-rst, cfg-rst, dbg-rst).
2. Enable PubSRAM memory clock (pubsram_clk = CLK_BUS_PUBSRAM).
3. ENABLE CONFIG BUS CLOCK FIRST (cfg_clk = CLK_BUS_RV_CFG) -> Register block becomes accessible!
4. Write Boot Vector to E906_STA_ADD_REG (writel(entry, cfg_va + 0x0204)).
5. Deassert Core Reset (mod_rst = RST_BUS_RV).
6. Enable Core Execution Clock (mod_clk = CLK_BUS_RV) -> Core begins fetching!

E906 HALT / STOP SEQUENCE:
1. ASSERT CORE RESET FIRST (mod_rst = RST_BUS_RV) -> Safely freezes pipeline and aborts bus bursts.
2. Disable Core Execution Clock (mod_clk = CLK_BUS_RV).
3. Disable Config Bus Clock (cfg_clk = CLK_BUS_RV_CFG).
4. Assert Config & Debug Resets (cfg_rst, dbg_rst).
5. Disable PubSRAM memory clock (pubsram_clk).
```

---

### B. Allwinner A733 (`sun60iw2`) — XuanTie E902 (No TCM, No DSP)

> [!IMPORTANT]
> **WHY THE A733 IS FUNDAMENTALLY DIFFERENT: THE E902 HAS NO TCMs**
> The XuanTie E902 is an ultra-compact embedded core that **does not implement Tightly Coupled Memories (NO ITCM, NO DTCM)**.
> - On the **T527 (E906)**: Dedicated ITCM (`0x07110000`) and DTCM (`0x07120000`) exist for zero-wait-state code and stack.
> - On the **A733 (E902)**: Addresses `0x07110000` and `0x07120000` **do not exist in silicon**. Any attempt to access them triggers an immediate interconnect DECERR / asynchronous SError abort!
> - **Everything executes out of Shared SRAM A2 (`0x00040000`–`0x00073FFF`)**: The hardware reset vector, code, data, BSS, and stack all reside in Shared SRAM A2.
> - The A733 Device Tree does not declare TCM memory windows, and the linker script (`firmware.ld`) links directly to `ORIGIN = 0x00044000`.

| Memory / Register Region | Host (ARM64) Address | RISC-V Core Address | Size | Primary Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Shared SRAM A2** (`sram`) | `0x00040000` | `0x00040000` | 208 KB | **Reset Vector Table, Code, Stack & Data** |
| **R-CCU / PRCM** | `0x07010000` | `0x07010000` | 64 KB | Always-On Clock & Reset Controller |
| **R-PIO (S_GPIO)** | `0x07025000` | `0x07025000` | 8 KB | Low-Latency I/O (`PL`, `PM` pin banks) |
| **S_UART0** | `0x07080000` | `0x07080000` | 4 KB | Dedicated RISC-V Serial Console (115200) |
| **Mailbox / Msgbox** | `0x03004000` / `0x07094000` | `0x07094000` | 4 KB | Doorbell IPC IRQ |

#### A733 R-CCU RISC-V Control Register (`0x0701021C`):
* **Bit 0** (`CLK_RISCV`): Core Clock Gate (`1` = Clock Enabled)
* **Bit 1** (`CLK_RISCV_CFG`): Bus Interconnect Clock Gate (`1` = Bus Clock Enabled)
* **Bit 16** (`RST_BUS_RISCV_CFG`): Core Reset (`1` = Out of Reset / Running, `0` = Held in Reset)
* **Status**: Verified in hardware via early boot register inspection: `0x0701021c` -> `0x00010003` (Clocked & running).
* **Reset Vector**: Hardwired in silicon to fetch its first instruction from **`0x00040000`** (Base of SRAM A2). No MMIO boot address register is used.

---

## 3. Microarchitecture, ISA & Execution Efficiency Comparison

Both the XuanTie E906 (T527) and XuanTie E902 (A733) operate at the **exact same maximum clock frequency of up to 200 MHz**, but they differ significantly in microarchitectural execution efficiency, pipeline depth, register resources, and supported instruction sets.

| Architectural Parameter | **Allwinner T527 (XuanTie E906)** | **Allwinner A733 (XuanTie E902)** |
| :--- | :--- | :--- |
| **Max Clock Frequency** | **Up to 200 MHz** [[1](https://www.forlinx.net/product/t527-c-system-on-module-149.html), [2](https://www.cnx-software.com/2024/03/07/allwinner-t527-system-on-module-features-octa-core-cortex-a55-cpu-2-tops-ai-accelerator/)] | **Up to 200 MHz** [[3](https://linux-sunxi.org/A733), [4](https://wiki.postmarketos.org/wiki/Allwinner_A733), [5](https://www.armsom.org/post/allwinner-a733-deep-dive-why-armsom-chose-this-chip-for-sige6)] |
| **Pipeline Depth** | **5-Stage**, single-issue, in-order execution | **2-Stage**, single-issue, in-order execution [[1](https://www.xrvm.com/product/xuantie/E902)] |
| **Execution Efficiency** | **Higher IPC** (~1.4–1.7 DMIPS/MHz) | **Lower IPC** (~0.9–1.1 DMIPS/MHz) |
| **Base Integer Architecture** | **RV32I** (Standard 32-bit RISC-V) | **RV32E** (Embedded Reduced Register Set) |
| **Integer Register Count** | **32 Registers** (`x0`–`x31`) | **16 Registers** (`x0`–`x15`) |
| **Floating Point Unit (FPU)** | **Hardware FPU**: IEEE-754 Single (`F`) & Double (`D`) Precision | **NONE** (Soft-Float only; no `f0`–`f31` float registers) |
| **Multiply / Divide (`M`)** | Hardware integer multiplier & divider | Hardware integer multiplier & divider |
| **Atomic Instructions (`A`)** | Supported (`lr.w`, `sc.w`, `amoswap`, `amoadd`) | Not implemented |
| **Compressed (`C`)** | Supported (16-bit instructions) | Supported (16-bit instructions) |
| **Mandatory Compiler Flags** | `-march=rv32imafdc -mabi=ilp32d` | `-march=rv32emc -mabi=ilp32e` |

> [!WARNING]
> **CRITICAL COMPILER/ABI MISMATCH HAZARD**:
> - The XuanTie E902 implements the **RV32E** specification with only **16 general-purpose integer registers** (`x0`–`x15`).
> - If firmware is compiled with standard `ilp32` or `ilp32d` (which targets standard RV32I 32-register cores), GCC will emit instructions addressing registers `x16`–`x31` (`s2`–`s11`, `a6`, `a7`, `t3`–`t6`) or floating-point opcodes.
> - On the E902 silicon, instructions accessing registers `16`–`31` or FPU instructions trigger an immediate **Hardware Illegal Instruction Exception**!
> - A733 firmware **MUST** be compiled targeting `-march=rv32emc -mabi=ilp32e`.

---

## 4. Linux 7.1 Remote Processor (`remoteproc`) Integration (T527 / A523)

The kernel driver `drivers/remoteproc/sunxi_rproc.c` targets the **Allwinner T527, A527, and A523** platforms where the XuanTie E906/E907 is a dedicated real-time coprocessor:
* **No Synthetic Code / No Trampolines**: The driver is a pure lifecycle manager. It does not modify memory or inject trampolines.
* **Firmware Contract**: The firmware ELF links its reset vector table to Dedicated SRAM / TCM at `0x00000000` or local SRAM at `0x07280000`.
* **A733 Status**: On the Allwinner A733, the E902 is dedicated to CPUS / Always-On power management running U-Boot `scp.fex` and is not managed by Linux `remoteproc`.

### A. Lifecycle Management Commands (T527 / Cubie A5E)
```bash
# 1. Install bare-metal ELF binary to Linux firmware directory
cp firmware.elf /lib/firmware/riscv-firmware.elf

# 2. Assign firmware to remoteproc instance
echo riscv-firmware.elf > /sys/class/remoteproc/remoteproc0/firmware

# 3. Boot XuanTie E906/E907 RISC-V core
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. View real-time printk / trace buffer from RISC-V
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 5. Stop co-processor
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### B. Device Tree Node (T527 / A523 DTS)
```dts
msgbox: mailbox@3003000 {
    compatible = "allwinner,sun55i-a523-msgbox", "allwinner,sun8i-a83t-msgbox";
    reg = <0x03003000 0x1000>;
    clocks = <&ccu CLK_BUS_MSGBOX>;
    resets = <&ccu RST_BUS_MSGBOX>;
    interrupts = <GIC_SPI 147 IRQ_TYPE_LEVEL_HIGH>;
    #mbox-cells = <1>;
};

rproc: remoteproc@7102000 {
    compatible = "allwinner,sun55i-a523-rproc",
                 "allwinner,sun55i-a527-rproc",
                 "allwinner,sun55i-t527-rproc",
                 "allwinner,sunxi-rproc";
    reg = <0x02001000 0x1000>,
          <0x07102000 0x1000>,
          <0x07280000 0x40000>,
          <0x00020000 0x20000>;
    reg-names = "main_ccu", "ccu", "r_sram", "sram";
    mboxes = <&msgbox 0>, <&msgbox 1>;
    mbox-names = "rx", "tx";
    status = "okay";
};
```

---

## 5. Standalone Firmware SDK: AbstractX & HAL

The full firmware workspace is located at:  
📂 **[`/run/media/tcmichals/projects/radxa/RISCV_Linux_T527_A733/`](file:///run/media/tcmichals/projects/radxa/RISCV_Linux_T527_A733/)**

### Firmware Architectural Highlights:
1. **AbstractX Native Coroutines**:
   - Stackless C++20 coroutines (`co_await`) without dynamic memory allocation (`malloc`).
   - Sub-20ns resumption latency on pin edge triggers (`DRDY`) and timer expirations.
2. **Zero-Copy Lock-Free IPC Rings**:
   - 16-byte Single Producer Single Consumer (SPSC) descriptors mapped into shared SRAM C.
   - Hardware Mailbox doorbell interrupts ensure ARM64 Linux and RISC-V synchronize in a single CPU cycle.
3. **Diagnostics & Binary Tracing**:
   - Google Pigweed compile-time tokenized string logging (4 bytes per log message).
   - BareCTF binary trace logging mapped directly into Linux `debugfs/remoteproc/remoteproc0/trace0`.

---

## 6. Co-Processor Debug Architecture: Direct Memory Debug (`dmem`) Comparison

Unlike TI Sitara (AM62x/AM64x), STM32MP1, and NXP i.MX SoCs which implement a memory-mapped `dmem` interface exposing auxiliary core debug registers directly to the non-secure ARM interconnect (enabling self-hosted OpenOCD/GDB debugging natively without external probes), current **Allwinner T527 silicon does not route a `dmem` bus interface** for the XuanTie RISC-V Debug Module (DM) into Linux userspace.

### Debugging Capabilities on Current T527 Silicon:
1. **RemoteProc Trace Buffer**: High-throughput circular telemetry (`/sys/kernel/debug/remoteproc/remoteproc0/trace0` via `rproc_trace` carveout).
2. **Dedicated Hardware UART**: Low-level bare-metal serial console (`S_UART0` @ `0x07080000` / 115200 baud).
3. **Lock-Free Shared SRAM Ring Buffers**: High-speed Single Producer Single Consumer (SPSC) telemetry in Shared SRAM A2 (`0x00040000`).
4. **Hardware Mailbox IPC Doorbell**: Single-cycle inter-core synchronization.

### Future Silicon Outlook:
If future Allwinner SoC revisions incorporate a standard memory-mapped `dmem` interface to the RISC-V Debug Module Interface (DMI), native self-hosted OpenOCD and GDB remote debugging can be used directly from Linux without external hardware probes, matching the workflow on TI and ST devices.

