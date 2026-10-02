# Tina SDK Technical Reference Manual (TRM): Allwinner E906 RISC-V & HiFi4 DSP Co-Processors
**Document Role**: Authoritative Technical Reference Manual (TRM) & Single Source of Truth for the Tina SDK Co-Processor Subsystems  
**Target Platforms**: Allwinner sun55i (A523 / A527 / T527 / Radxa Cubie A5E) & sun60i (A733 / Radxa Cubie A7A)  
**Co-Processors**: Alibaba T-Head XuanTie E906 RISC-V & Cadence Tensilica HiFi4 Audio DSP  
**Primary Reference Sources**: Tina RTOS HAL (`product/tina/rtos-hal`), Tina RTOS Core (`product/tina/rtos`), Tina DSP (`lichee/dsp`), Tina IPC (`product/rtos-components`), and Linux BSP (`linux-aw2501`)

---

## 1. Upstream & Upstream-Adjacent Repository Preservation & Acquisition

To ensure the permanence of this technical specification even if upstream Gitlab or cloud mirrors disappear, the exact repository origins, branches, and commit hashes analyzed are permanently cataloged below.

### 1.1 Master Repository Metadata Table

| Repository / Subsystem | Remote URL | Release / Branch | Verified Commit Hash | Key Directory / Driver Paths |
| :--- | :--- | :--- | :--- | :--- |
| **Tina RTOS HAL** | `https://gitlab.com/tina5.0_aiot/product/tina/rtos-hal.git` | `master` (`aiot-linux-v1.5.0`) | `cc63790c369bf6c75173b333c61d03fb70ad3e72` | `hal/source/dma/`, `hal/source/uart/`, `hal/source/spi/`, `hal/source/twi/`, `hal/source/ccmu/` |
| **Tina RTOS Core** | `https://gitlab.com/tina5.0_aiot/product/tina/rtos.git` | `master` (`aiot-linux-v1.5.0`) | `a9e0afd594e8a67e13c173c41d164df532d29884` | `arch/risc-v/sun55iw3p1/sun55i.c`, `arch/risc-v/e90x/`, `kernel/FreeRTOS/` |
| **Tina DSP (HiFi4)** | `https://gitlab.com/tina5.0_aiot/lichee/dsp.git` | `master` (`aiot-linux-v1.5.0`) | `9b7f171fb4d01aed467635e267a013b2ae640197` | `arch/sun55iw3/`, `arch/sun55iw3/lsp/dsp0/default_t527/memmap.xmm`, `kernel/FreeRTOS_xtensa_v1.7/` |
| **Tina Components** | `https://gitlab.com/tina5.0_aiot/product/rtos-components.git` | `master` (`aiot-linux-v1.5.0`) | `190e301e2ef6958936abed06790f4734ab854e26` | `aw/amp/amp_msgbox.c`, `thirdparty/openamp/` |
| **Radxa Linux BSP** | `https://github.com/radxa/kernel.git` | `linux-5.15` / `linux-6.6` | Downstream Radxa / Allwinner | `drivers/remoteproc/sunxi_rproc_e906_boot.c`, `drivers/mailbox/sunxi-msgbox.c`, `arch/arm64/boot/dts/allwinner/sun55iw3p1.dtsi` |

---

### 1.2 Step-by-Step Acquisition (No Baidu, No Paywalls, No Registration)

All Allwinner Tina 5.0 co-processor repositories can be cloned directly over standard HTTPS without third-party cloud utilities, Baidu Netdisk clients, or commercial paywalls:

```bash
# 1. Create a dedicated workspace for Tina source preservation
mkdir -p allwinner_tina_source && cd allwinner_tina_source

# 2. Clone Tina RTOS Hardware Abstraction Layer (HAL)
git clone https://gitlab.com/tina5.0_aiot/product/tina/rtos-hal.git rtos-hal
git -C rtos-hal checkout cc63790c369bf6c75173b333c61d03fb70ad3e72

# 3. Clone Tina RTOS Kernel (FreeRTOS kernel & XuanTie E906 CLIC/PMP)
git clone https://gitlab.com/tina5.0_aiot/product/tina/rtos.git rtos
git -C rtos checkout a9e0afd594e8a67e13c173c41d164df532d29884

# 4. Clone Tina Cadence Tensilica HiFi4 Audio DSP Stack
git clone https://gitlab.com/tina5.0_aiot/lichee/dsp.git dsp
git -C dsp checkout 9b7f171fb4d01aed467635e267a013b2ae640197

# 5. Clone Tina IPC & OpenAMP Middleware (RPMsg, VirtIO, Msgbox)
git clone \
  https://gitlab.com/tina5.0_aiot/product/rtos-components.git rtos-components
git -C rtos-components checkout 190e301e2ef6958936abed06790f4734ab854e26

# 6. Clone Radxa Downstream Host Linux Kernel
git clone --depth 1 -b linux-5.15 \
  https://github.com/radxa/kernel.git radxa-linux-5.15
```

---

### 1.3 Toolchains: How to Obtain & Configure

#### A. RISC-V XuanTie E906 Compiler (`RV32IMAFCX`)
The E906 core requires a 32-bit RISC-V cross-compiler with single-precision floating point (`-march=rv32imafc -mabi=ilp32f`):
* **Verified Local Host Toolchain**:
  Installed at `~/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/` (xPack GNU RISC-V Embedded GCC 15.2.0 supporting C++20/C++23):
  ```bash
  export PATH="$HOME/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
  riscv-none-elf-gcc --version   # Confirmed: xPack GCC 15.2.0
  riscv-none-elf-g++ --version
  ```
* **Option 1 (T-Head Official Optimized Toolchain)**:
  Download the XuanTie GNU Toolchain from Alibaba T-Head GitHub (`riscv64-unknown-elf-gcc` supporting `-march=rv32imafc[xthead]`):
  ```bash
  wget https://github.com/XUANTIE-RV/xuantie-gnu-toolchain/releases/download/v2.8.1/xuantie-gnu-toolchain-rv32imafc-linux-x86_64.tar.gz
  ```
* **Option 2 (Standard Upstream xPack GNU RISC-V Embedded GCC)**:
  Standard open-source upstream GCC 12/13/14/15 fully supports RV32IMAFC:
  ```bash
  # Pre-built cross-compiler
  sudo apt-get install gcc-riscv64-unknown-elf
  # Or xPack portable distribution
  wget https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v13.2.0-2/xpack-riscv-none-elf-gcc-13.2.0-2-linux-x64.tar.gz
  ```

#### B. Cadence Tensilica HiFi4 DSP Compiler
* **Commercial Vendor Path**: Cadence licenses the Xtensa Development Tools (XDT: `xt-xcc`, `xt-clang`, `xt-ld`) alongside core licensing. Allwinner SDKs package pre-compiled LSP files under `arch/sun55iw3/lsp/dsp0/default_t527/`.
* **Open-Source GNU Xtensa Path**:
  Mainline binutils/GCC support Xtensa. To build for the Allwinner HiFi4 configuration without Cadence licensing, apply the `xtensa-overlays` configuration patch (`config/default_t527`) to GCC:
  ```bash
  git clone https://github.com/foss-xtensa/xtensa-overlays.git
  ```

---

## 2. Executive Summary & Silicon Identity: E906 vs. E907

### 2.1 The Definitive Fact: The Silicon Core is an E906
Marketing collateral, board spec sheets (e.g. Radxa, Forlinx promotional summaries), and early blog posts frequently refer to the co-processor as an **"E907"**. 

**On physical silicon, in hardware RTL, and in Allwinner's engineering code, the co-processor is 100% the Alibaba T-Head XuanTie E906.**

### 2.2 Incontrovertible Proofs

#### Proof 1: Hardware CSR Read on Silicon (`misa = 0x40901125`)
When querying the Machine ISA CSR (`misa`) directly from the RISC-V core on physical silicon (verified on live hardware in `cubie-a5e/firmware/tests.md` line 782), the register returns:
```text
misa = 0x40901125
```

Decoding `0x40901125` per the RISC-V Privileged Architecture Specification:
* **Bit [31:30] = `01`**: Base architecture is 32-bit (`RV32`).
* **Bit 0 (`A`) = `1`**: Atomic extension present.
* **Bit 2 (`C`) = `1`**: Compressed instruction extension present.
* **Bit 3 (`D`) = `0`**: **NO Double-Precision Floating Point.**
* **Bit 5 (`F`) = `1`**: Single-Precision Floating Point present.
* **Bit 8 (`I`) = `1`**: Base Integer Instruction set.
* **Bit 12 (`M`) = `1`**: Integer Multiply/Divide extension present.
* **Bit 15 (`P`) = `0`**: **NO Packed-SIMD / DSP Extension.**
* **Bit 20 (`U`) = `1`**: User mode privilege supported.
* **Bit 23 (`X`) = `1`**: T-Head non-standard vendor extensions present.

**The Architectural Core Distinction:**
* **XuanTie E907**: Synthesized with `D` (Double-precision float) and `P` (T-Head DSP/Packed-SIMD math). Bit 15 and Bit 3 must both be `1`.
* **XuanTie E906**: Synthesized as `RV32IMAFCX` (Single-precision float, no `D`, no `P`).  
Because **Bit 3 (`D`) = 0** and **Bit 15 (`P`) = 0**, the silicon is unequivocally an **E906**.

#### Proof 2: Allwinner Silicon Kconfig (`rtos/arch/risc-v/Kconfig`)
Allwinner's chip synthesis definitions explicitly select the E906 for sun55i and sun60i:
```kconfig
config ARCH_SUN55IW3
    bool "allwinner sun55iw3"       # A523 / T527 (Radxa Cubie A5E)
    select ARCH_RISCV_E906

config ARCH_SUN60IW1
    bool "allwinner sun60iw1"       # A733 (Radxa Cubie A7A)
    select ARCH_RISCV_E906

config ARCH_SUN55IW6
    bool "allwinner sun55iw6"       # Different revision
    select ARCH_RISCV_E907
```

#### Proof 3: Hardware Register Base & RTL Module Names
* The configuration register block at `0x07130000` is named `E906_VER_REG`, `E906_STA_ADD_REG` in hardware headers.
* The Linux BSP driver is `sunxi_rproc_e906_boot.c`.
* The Devicetree node is `e906_rproc: e906_rproc@7130000`.

---

## 3. Cadence Tensilica HiFi4 DSP Compiler Architecture

### 3.1 Why Upstream GCC/Clang Cannot Compile for HiFi4 Directly
A frequent point of confusion is how Allwinner compiles firmware for the Cadence Tensilica HiFi4 DSP.

1. **Tensilica Processor Generator Model**:
   * Cadence Tensilica does not sell fixed silicon cores like ARM Cortex. Instead, licensees configure a parameterized Xtensa core (vector units, VLIW slot widths, SIMD execution units, TCM interfaces, and customized audio instructions).
   * Tensilica's proprietary core build system (**Xtensa Development Tools / XDT / Tensilica Xplorer**) automatically generates a customized compiler toolchain matching the exact RTL netlist:
     * `xt-xcc` / `xt-clang`: C/C++ compiler with intrinsics for 24-bit/32-bit dual/quad-MAC audio arithmetic (`AE_*` audio intrinsics).
     * `xt-ld`: Linker aware of custom TCM and vector alignment requirements.
     * `xt-objcopy`, `xt-strip`, `xt-ar`.
2. **Local Support Package (LSP)**:
   * The directory `scratch/dsp/arch/sun55iw3/lsp/dsp0/default_t527/` contains the machine description:
     * `memmap.xmm`: Exact memory layout mapping IRAM, DRAM, and DDR segments.
     * `specs`: Toolchain specs file telling `xt-xcc` how to map sections.
3. **Open-Source Path: Xtensa Overlays (`xtensa-overlays`)**:
   * While mainline GNU GCC has an `xtensa-unknown-elf` target, it only targets the base Xtensa architecture (e.g. ESP32).
   * To build HiFi4 code using open-source GCC, one must apply the Allwinner HiFi4 **Xtensa Configuration Overlay** (`xtensa-config.h`, `xtensa-modules.c`) to GNU binutils and GCC.

---

## 4. Clock Hierarchy & Strict Domain Partitioning (Linux vs. FreeRTOS)

A critical architectural hazard in heterogeneous embedded design is **clock contention**. If FreeRTOS attempts to reconfigure system PLLs, the ARM Cortex-A55 Linux host will crash. The Allwinner SoC enforces a strict two-stage clock hierarchy:

```text
                       +-----------------------------+
                       |     24 MHz DCXO Crystal     |
                       +-----------------------------+
                                      |
                 +--------------------+--------------------+
                 |                                         |
                 v                                         v
+---------------------------------+       +---------------------------------+
| 1. LINUX HOST DOMAIN            |       | 2. ALWAYS-ON / RTC DOMAIN       |
|    (Main CCU: 0x02001000)       |       |    (R_CCU: 0x07010000)          |
| [EXCLUSIVE OWNER: LINUX KERNEL] |       | [SHARED OR DELEGATED]           |
|                                 |       |                                 |
| - PLL_PERI0_2X   (1200MHz Root) |       | - R_AHB Bus Clock               |
| - PLL_PERI0_300M (300MHz Root)  |       | - R_UART0 / R_UART1 Clock       |
| - PLL_AUDIO0 / PLL_AUDIO1       |       | - R_SPI Clock                   |
| - Root Dividers for CLK_DSP     |       | - R_TWI0 / R_TWI1 Clock         |
|   (600MHz) & CLK_BUS_RV (200MHz)|       |                                 |
+---------------------------------+       +---------------------------------+
                 |
                 v
+---------------------------------------------------------------------------+
| 3. CO-PROCESSOR / MCU DOMAIN (MCU CCU: 0x07110000)                        |
| [SHARED CONTROL: LINUX GATES/RESETS, CO-PROCESSOR CONSUMES CLOCK]         |
|                                                                           |
| - RV Module Clock Gate (rv_clk, 0x0120) & Reset (RST_BUS_RV, 0x0124)      |
| - MCU DMA Clock Gate (mcu_dma_clk, 0x0104) & Reset (RST_BUS_MCU_DMA)     |
| - MCU Timers 0..5 Clock Dividers (0x0074 - 0x0088)                        |
| - DSP Module Clock Gate (dsp_dsp_clk, 0x0020) & Reset (RST_BUS_DSP,0x100)|
+---------------------------------------------------------------------------+
                 |
                 v
+---------------------------------------------------------------------------+
| 4. PERIPHERAL INTERNAL DIVIDERS (Local Peripheral Register Blocks)        |
| [100% OWNED BY CO-PROCESSOR: FREERTOS / BARE-METAL FIRMWARE]              |
|                                                                           |
| - UART: 16-Bit Divisor Latch (DLL/DLH): Divisor = Fin / (16 * Baud)       |
| - SPI:  Clock Control Register (SPI_CCR): Fsclk = Fin / (2^N * (M + 1))   |
| - TWI:  Clock Control Register (TWI_CCR): Fscl = Fin / (2^N * (M+1) * 10) |
+---------------------------------------------------------------------------+
```

### 4.1 Strict Partitioning Rules: Linux Setup vs. FreeRTOS Consumption

1. **Linux Exclusivity over Main CCU (`0x02001000`)**:
   * Linux exclusively owns, programs, and gates root PLLs (`PLL_PERI0`, `PLL_AUDIO`, etc.) and system bus dividers.
   * FreeRTOS / co-processor firmware **MUST NEVER** write to Main CCU registers (`0x02001000 - 0x02001FFF`). Any write risks destabilizing the ARM Cortex-A55 Linux host.

2. **Linux Host RemoteProc Setup Sequence (E906 RISC-V)**:
   * Linux RemoteProc boot driver (`sunxi_rproc_e906_boot.c`):
     1. Maps MCU CCU (`0x07110000`).
     2. Sets parent clock of `CLK_BUS_RV` to `PLL_PERI0_300M` or `DCXO24M`.
     3. Configures parent divider to achieve `200,000,000 Hz` (200 MHz).
     4. Ungates `pubsram`, `rv-cfg`, and `rv` clocks in `MCU_CCU` (`0x0120`).
     5. Deasserts debug, configuration, and core module resets in `RST_BUS_RV` (`0x0124`).

3. **Linux Host RemoteProc Setup Sequence (HiFi4 Audio DSP)**:
   * Linux RemoteProc DSP driver (`sunxi_rproc_dsp.c`):
     1. Maps MCU CCU (`0x07110000`).
     2. Sets parent clock of `CLK_DSP` (offset `0x0020`) to `PLL_PERI0_2X` (1200 MHz) with divider `/2` to achieve `600,000,000 Hz` (600 MHz).
     3. Ungates DSP core clock and bus gate in `dsp_dsp_clk` (`0x0020`).
     4. Deasserts DSP bus reset in `RST_BUS_DSP` (`0x0100`).

4. **What FreeRTOS Does During Initialization (Consumption Only)**:
   * **Boot Invariant**: Both co-processors boot with their core clocks **already running** at target frequencies (200 MHz for E906, 600 MHz for HiFi4).
   * **HAL Clock Init**: Inside `prvSetupHardware()`, FreeRTOS invokes `hal_clock_init()` (`hal/source/ccmu/hal_clock.c`). This registers the static software clock tree in memory and probes clock frequencies; **it does not modify root PLLs**.
   * **Peripheral Baud/Rate Control**: FreeRTOS and co-processor drivers adjust peripheral communication rates **solely via local peripheral divisor registers**:
     - UART: 16-bit Divisor Latch Registers (`DLL` / `DLH`)
     - SPI: Clock Control Register (`SPI_CCR`, `Fsclk = Fin / (2^N * (M + 1))`)
     - TWI / I2C: Clock Control Register (`TWI_CCR`, `Fscl = Fin / (2^CLK_N * (CLK_M + 1) * 10)`)


---

## 5. Technical Reference Manual: Complete Register Specifications

### 5.1 E906 Configuration Registers (`0x07130000`)

| Offset | Register Name | Type | Reset Value | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000` | `E906_VER_REG` | RO | `0x00010000` | Hardware IP Version Register |
| `0x0010` | `E906_RF1P_CFG_REG` | RW | `0x00000000` | Core SRAM / Register File Power Configuration |
| `0x0204` | `E906_STA_ADD_REG` | RW | `0x00000000` | **Reset Vector PC Entry Point** (e.g. `0x60000000` or `0x00040000`) |
| `0x0220` | `E906_WAKEUP_EN_REG`| RW | `0x00000000` | Core Wakeup Event Enable Register |
| `0x0248` | `E906_WORK_MODE_REG`| RW | `0x00000000` | Power Saving / Low Power Work Mode Control |

---

### 5.2 HiFi4 DSP Configuration Registers (`0x07100000`)

| Offset | Register Name | Type | Reset Value | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000` | `HIFI4_CTRL_REG0` | RW | `0x00000001` | Control Register 0: Bit 0 = `RUN_STALL`, Bit 1 = `START_VEC_SEL`, Bit 2 = `CLKEN` |
| `0x0004` | `HIFI4_ALT_RESET_VEC`| RW | `0x00000000` | **Alternate Reset Vector** (Default: `0x00400660` or DDR `0x3a000200`) |
| `0x0010` | `HIFI4_STAT_REG` | RO | `0x00000000` | DSP Core Execution Status Register |
| `0x0364` | `SRAM_REMAP_REG` (`0x07010364`) | RW | `0x00000001` | SRAM Remap: Bit 0 = `1` (CPUX access), Bit 0 = `0` (DSP access) |

---

### 5.3 Hardware Message Box (Mailbox) Registers

Physical bases: ARM = `0x03003000`, DSP = `0x07120000`, CPUS = `0x07094000`, RISC-V = `0x07136000`.  
Register offset address calculation formula for remote core $n$ and channel $p \in \{0, 1, 2, 3\}$:
$$\text{Address} = \text{CoreBase} + \text{RegOffset} + (n \times 0x100) + (p \times 0x4)$$

| Register Offset | Register Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `0x20 + n*0x100` | `MSGBOX_RD_IRQ_EN_REG` | RW | Read Interrupt Enable (Bit $2p$ = 1 enables IRQ for Channel $p$) |
| `0x24 + n*0x100` | `MSGBOX_RD_IRQ_STA_REG`| W1C | Read Interrupt Status (Bit $2p$ = 1 indicates message pending; write 1 to clear) |
| `0x30 + n*0x100` | `MSGBOX_WR_IRQ_EN_REG` | RW | Write FIFO Not Full Interrupt Enable |
| `0x34 + n*0x100` | `MSGBOX_WR_IRQ_STA_REG`| W1C | Write FIFO Not Full Interrupt Status |
| `0x50 + n*0x100 + p*0x4`| `MSGBOX_FIFO_STA_REG`| RO | FIFO Full Flag (Bit 0: `1` = FIFO Full, `0` = Space Available) |
| `0x60 + n*0x100 + p*0x4`| `MSGBOX_MSG_STA_REG` | RO | Message Count (Bits [2:0] = Number of unread words in FIFO) |
| `0x70 + n*0x100 + p*0x4`| `MSGBOX_MSG_REG` | RW | **32-Bit FIFO Data Port** (Read pops from FIFO; write pushes to FIFO) |

#### Routing Index $n$ Matrix (`to_coef_n[local][remote]`):
* **Linux $\leftrightarrow$ RISC-V**: $n = 2$
* **Linux $\rightarrow$ DSP**: $n = 0$
* **DSP $\rightarrow$ Linux**: $n = 1$

---

## 6. High-Performance DMA Engine (DMA0) Architecture

The sun55i SoC integrates two DMA engines:
* **DMA0** (`0x03002000`): Dedicated to **Linux Host CPUX and XuanTie E906 RISC-V** (`SUNXI_USED_DMA0 = 1`).
* **DMA1** (`0x07121000`): Dedicated to **HiFi4 Audio DSP** (`SUNXI_USED_DMA1 = 1`).

### 6.1 Channel Allocation Matrix (DMA0: 16 Channels Total)
* **Channels 0 to 7**: Allocated to Linux Host ARM Cortex-A55.
* **Channels 8 to 15**: **Dedicated exclusively to XuanTie E906 RISC-V** (`DMA_START_CHAN = 8`).

### 6.2 DMA0 Register Map (`Base: 0x03002000`)

| Offset | Register Name | Description |
| :--- | :--- | :--- |
| `0x0000` | `DMA_IRQ_EN_REG0` | Interrupt Enable for Channels 0 to 7 |
| `0x0004` | `DMA_IRQ_EN_REG1` | **Interrupt Enable for Channels 8 to 15 (E906 Channels)** |
| `0x0010` | `DMA_IRQ_STAT_REG0` | Interrupt Pending for Channels 0 to 7 |
| `0x0014` | `DMA_IRQ_STAT_REG1` | **Interrupt Pending for Channels 8 to 15 (W1C)** |
| `0x0020` | `DMA_SEC_REG` | DMA Security Configuration Register |
| `0x0028` | `DMA_GATE_REG` | DMA Auto-Gating Control Register |
| `0x0030` | `DMA_STAT_REG` | Channel Busy Status Register (Bits 0..15) |
| `0x0034` | `DMA_CPU_IRQ_CTRL` | CPU IRQ Routing Mask (`0x00FF`) |
| `0x0038` | `DMA_MCU_IRQ_CTRL` | **MCU/E906 IRQ Routing Mask (`0xFF00`)** |

#### Channel Register Blocks (Channel $k \in \{8 \dots 15\}$, Base: `0x03002100 + k * 0x40`)

| Channel Offset | Register Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `+ 0x00` | `DMA_CHAN_ENABLE` | RW | Bit 0: `1` = Channel Enabled, `0` = Channel Disabled |
| `+ 0x04` | `DMA_CHAN_PAUSE` | RW | Bit 0: `1` = Pause Transfer, `0` = Resume Transfer |
| `+ 0x08` | `DMA_CHAN_LLI_ADDR` | RW | **32-Bit Physical Address of Linked List Descriptor (LLI)** |
| `+ 0x0C` | `DMA_CHAN_CFG` | RO | Current Channel Configuration (Loaded from LLI) |
| `+ 0x10` | `DMA_CHAN_CUR_SRC` | RO | Current Source Physical Address |
| `+ 0x14` | `DMA_CHAN_CUR_DST` | RO | Current Destination Physical Address |
| `+ 0x18` | `DMA_CHAN_CNT` | RO | Bytes Remaining in Current Transfer |
| `+ 0x1C` | `DMA_CHAN_PARA` | RO | Parameter Register (Clock cycles, handshake delays) |

---

### 6.3 32-Byte Linked List Descriptor (`sunxi_dma_lli`) Structure

Allwinner DMA uses a hardware scatter-gather descriptor table loaded directly from SRAM or DDR:

```c
struct sunxi_dma_lli {
    uint32_t cfg;      /* +0x00: Channel config (DRQ, Burst, Width) */
    uint32_t src;      /* +0x04: Source physical memory address */
    uint32_t dst;      /* +0x08: Destination physical memory address */
    uint32_t len;      /* +0x0C: Total transfer length in bytes */
    uint32_t para;     /* +0x10: Handshake parameter and wait clocks */
    uint32_t p_lln;    /* +0x14: Next physical descriptor (0xFFFFF800=End) */
    uint32_t vlln;     /* +0x18: Virtual pointer (Driver software use) */
    uint32_t res;      /* +0x1C: Reserved padding for 32B alignment */
} __attribute__((aligned(32)));
```

#### Descriptor Configuration Word (`cfg`) Bitfield Map:
* **Bits [4:0] (`SRC_DRQ`)**: Source Hardware DRQ Request Line (0..63)
* **Bits [7:6] (`SRC_BURST`)**: Source Burst Length (`00` = 1, `01` = 4, `10` = 8, `11` = 16)
* **Bit 8 (`SRC_ADDR_MODE`)**: `0` = Linear Incrementing (Memory), `1` = Fixed I/O (FIFO)
* **Bits [10:9] (`SRC_WIDTH`)**: Source Bus Width (`00` = 8-bit, `01` = 16-bit, `10` = 32-bit)
* **Bits [20:16] (`DST_DRQ`)**: Destination Hardware DRQ Request Line (0..63)
* **Bits [23:22] (`DST_BURST`)**: Destination Burst Length (`00` = 1, `01` = 4, `10` = 8, `11` = 16)
* **Bit 24 (`DST_ADDR_MODE`)**: `0` = Linear Incrementing (Memory), `1` = Fixed I/O (FIFO)
* **Bits [26:25] (`DST_WIDTH`)**: Destination Bus Width (`00` = 8-bit, `01` = 16-bit, `10` = 32-bit)

---

### 6.4 Complete DRQ Request Line Routing Table (sun55iw3 / E906)

These exact hardware DRQ numbers configure the DMA0 multiplexer for hardware-paced streaming:

| Peripheral | Direction | DRQ Line Number | Peripheral FIFO Register Physical Address |
| :--- | :--- | :--- | :--- |
| **SRAM / DDR Memory**| Memory | `0` | Any Physical Address |
| **UART0** | RX / TX | `14` | `0x02500000 + 0x00` (`UART_RBR` / `UART_THR`) |
| **UART1** | RX / TX | `15` | `0x02500400 + 0x00` |
| **UART2** | RX / TX | `16` | `0x02500800 + 0x00` |
| **UART3** | RX / TX | `17` | `0x02500C00 + 0x00` |
| **UART4** | RX / TX | `18` | `0x02501000 + 0x00` |
| **UART5** | RX / TX | `19` | `0x02501400 + 0x00` |
| **UART6** | RX / TX | `20` | `0x02501800 + 0x00` |
| **UART7** | RX / TX | `21` | `0x02501C00 + 0x00` |
| **R_UART0** (Low Power)| RX / TX | `51` | `0x07080000 + 0x00` |
| **R_UART1** (Low Power)| RX / TX | `52` | `0x07080400 + 0x00` |
| **SPI0** | RX / TX | `22` | `0x04025000 + 0x300` (`SPI_RXD_REG` / `SPI_TXD_REG`) |
| **SPI1** | RX / TX | `23` | `0x04026000 + 0x300` |
| **SPI2** | RX / TX | `24` | `0x04027000 + 0x300` |
| **R_SPI** (Low Power) | RX / TX | `53` | `0x07092000 + 0x300` |
| **TWI0 (I2C0)** | RX / TX | `43` | `0x02502000 + 0x0C` (`TWI_DATA_REG`) |
| **TWI1 (I2C1)** | RX / TX | `44` | `0x02502400 + 0x0C` |
| **TWI2 (I2C2)** | RX / TX | `45` | `0x02502800 + 0x0C` |
| **TWI3 (I2C3)** | RX / TX | `46` | `0x02502C00 + 0x0C` |
| **TWI4 (I2C4)** | RX / TX | `47` | `0x02503000 + 0x0C` |
| **TWI5 (I2C5)** | RX / TX | `48` | `0x02503400 + 0x0C` |
| **R_TWI0** | RX / TX | `49` | `0x07081400 + 0x0C` |
| **R_TWI1** | RX / TX | `50` | `0x07081800 + 0x0C` |
| **GPADC0** | Capture | `12` | `0x02009000 + 0x80` (`GPADC_DATA_REG`) |
| **GPADC1** | Capture | `13` | `0x02009400 + 0x80` |

---

## 7. Deep-Dive: Peripheral DMA Workflows & Hardware Programming Models

Traditional co-processor drivers often read and write peripherals on an **interrupt-per-byte** basis:
* **The Cost**: Each byte received on a 3 MBaud UART or 20 MHz SPI triggers a context switch (saving 32 integer registers, FPU state, stack adjustment). At 300,000 interrupts/sec, the 200 MHz core wastes 80% to 100% of its CPU time doing useless context thrashing.
* **The Zero-Copy DMA Solution**: The Allwinner DMA engine streams data directly between memory and peripheral FIFOs using hardware descriptors (Linked-List Items / LLI). The CPU executes 0 instructions during transfers. A completion interrupt or cyclic ring boundary notifies the driver with minimal latency.

---

### 7.1 SPI Controller High-Speed DMA Pipeline

1. **Hardware Registers**:
   * Base: `0x04025000` (SPI0)
   * `SPI_FCR_REG` (`0x0018`): FIFO Control Register
     * Bit 31: `FIFO_ACC_MOD` (`0` = Byte access, `1` = 32-bit Word access)
     * Bit 24: `RX_DRQ_EN` (`1` = Enable DMA RX Hardware Trigger)
     * Bits [23:16]: `RX_TRIG_LEVEL` (Set to 32 bytes)
     * Bit 8: `TX_DRQ_EN` (`1` = Enable DMA TX Hardware Trigger)
     * Bits [7:0]: `TX_TRIG_LEVEL` (Set to 32 bytes)
   * `SPI_TXD_REG` / `SPI_RXD_REG` (`0x0300`): FIFO Access Port
2. **Hardware DMA Programming Flow**:
   * Setup TX DMA Descriptor: `src = tx_buf`, `dst = 0x04025300`, `src_mode = linear`, `dst_mode = IO`, `dst_drq = 22`, `burst = 4`, `width = 8-bit/32-bit`.
   * Setup RX DMA Descriptor: `src = 0x04025300`, `dst = rx_buf`, `src_mode = IO`, `dst_mode = linear`, `src_drq = 22`.
   * Enable `TX_DRQ_EN` and `RX_DRQ_EN` in `SPI_FCR_REG`.
   * Start DMA Channels via `hal_dma_start()` or writing `DMA_EN_REG`.
   * Await completion interrupt or poll channel status (`DMA_STATUS_REG`).

---

### 7.2 Serial / UART Cyclic DMA Ring Buffer

For high-throughput telemetry, GPS streams, or CLI handling:

1. **Hardware Registers**:
   * Base: `0x02500000` (UART0)
   * `UART_FCR` (`0x0008`): FIFO Control Register
     * Bit 0: `FIFO_EN` (`1` = Enable 64-byte FIFO)
     * Bit 3: `DMA_MODE` (`1` = DMA Mode 1: DRQ asserted continuously while FIFO contains data)
     * Bits [7:6]: `RX_TRIGGER` (`01` = Trigger at 16 bytes)
2. **Cyclic Ring Buffer Setup (`hal_dma_prep_cyclic`)**:
   * Allocate circular ring buffer in SRAM (e.g. 4096 bytes).
   * Construct 2 chained LLI descriptors:
     * Desc A: Transfers bytes 0..2047 $\rightarrow$ points to Desc B.
     * Desc B: Transfers bytes 2048..4095 $\rightarrow$ points back to Desc A.
   * DMA runs indefinitely in hardware with **zero CPU cycles**.
   * Half-transfer and Full-transfer interrupts notify the driver, updating the consumer ring pointer without dropping a single byte at multi-megabaud rates.

---

### 7.3 TWI / I2C Bus Master DMA Transfers

Allwinner Sun55i TWI hardware provides two distinct programming models:
1. **Legacy Byte-FSM Mode** (offsets `0x00` - `0x18`): Byte-by-byte status-driven state machine (`TWI_STAT = 0x08, 0x18, 0x28, 0x50...`). Ideal for single-byte register reads/writes.
2. **Enhanced TWI Driver Mode with Hardware FIFO & DMA Engine** (offsets `0x0200` - `0x0304`):
   * Base: `0x02502000` (TWI0), `0x02502400` (TWI1), etc.
   * `TWI_DRV_CTRL` (`0x0200`): Transmission Control Register (`START_TRAN = 1`, `RESTART_TRAN = 2`, `READ_TRAN = 4`)
   * `TWI_DRV_CFG` (`0x0204`): Transmission Configuration (`PACKET_CNT` [15:0], `INTERVAL` [23:16])
   * `TWI_DRV_SLV` (`0x0208`): Slave ID (`SLV_ID` [15:9], `SLV_RD_CMD` [8], `SLV_ID_X` [7:0])
   * `TWI_DRV_FMT` (`0x020C`): Packet Format (`DATA_BYTE_CNT` [15:0], `ADDR_BYTE_CNT` [23:16])
   * `TWI_DRV_BUS_CTRL` (`0x0210`): Bus Control (`CLK_DUTY` [15], `CLK_N` [14:12], `CLK_M` [11:8])
   * `TWI_DRV_INT_CTRL` (`0x0214`): Interrupt Control (`TRAN_COM_INT` bit 16, `TRAN_ERR_INT` bit 17, `TX_REQ_INT` bit 18, `RX_REQ_INT` bit 19)
   * `TWI_DRV_DMA_CFG` (`0x0218`): DMA Configuration:
     * Bit 8: `DMA_TX` (`1` = Enable DMA TX Hardware Trigger)
     * Bit 24: `DMA_RX` (`1` = Enable DMA RX Hardware Trigger)
     * Bits [5:0] / [21:16]: `TRIG_LEVEL` (FIFO watermarks, default 16)
   * `TWI_DRV_FIFO_CON` (`0x021C`): FIFO Content Status and Reset:
     * Bits [5:0]: Send FIFO Count, Bit 6: Send FIFO Clear
     * Bits [21:16]: Recv FIFO Count, Bit 22: Recv FIFO Clear
   * `TWI_DRIVER_SENDF` (`0x0300`): 32-byte Send Data FIFO Port (target address for DMA TX)
   * `TWI_DRIVER_RECVF` (`0x0304`): 32-byte Receive Data FIFO Port (source address for DMA RX)
   * **DRQ Port**: 43 (`DRQDST_TWI0_TX = 43`, `DRQSRC_TWI0_RX = 43`).

#### TWI DMA Programming Flow
* For transfers `>= DMA_THRESHOLD` (32 bytes):
  1. Configure `TWI_DRV_SLV` with target 7-bit slave address and read/write bit.
  2. Configure `TWI_DRV_FMT` with byte lengths.
  3. Clear FIFOs via `TWI_DRV_FIFO_CON |= (1 << 6) | (1 << 22)`.
  4. Enable DMA handshake via `TWI_DRV_DMA_CFG |= DMA_TX` (or `DMA_RX`).
  5. Setup LLI descriptor targeting `0x02502300` (TX) or `0x02502304` (RX) with `DRQ = 43`.
  6. Enable interrupts in `TWI_DRV_INT_CTRL` (`TRAN_COM_INT | TRAN_ERR_INT`).
  7. Start DMA transfer via `hal_dma_start()` or writing `DMA_EN_REG`.
  8. Assert `START_TRAN` in `TWI_DRV_CTRL`.

---

### 7.4 GPIO and DMA Interaction

* **Silicon Reality**: Digital GPIO pins on sun55i do **not** have dedicated DRQ lines in the DMA multiplexer.
* **Deterministic High-Rate Waveform Generation**:
  * Paced DMA transfers: A hardware Timer (e.g. `mcu_timer0`) or PWM DMA stream can pump data directly to `SUNXI_GPIO_PBASE + 0x10 * port` (Data Out Register). This achieves bit-banging waveforms (e.g. WS2812 LED strings, stepped stepper motor pulse trains) at exact hardware clock intervals without software jitter.
* **Sub-Microsecond Edge Interrupt Dispatching**:
  * For input events, GPIO edge triggers bypass the slow OS queue and enter the T-Head CLIC fast interrupt handler, executing the ISR in under 400 nanoseconds.

---

### 7.5 DMA vs. ISR / FIFO Threshold Balancing: Architectural Trade-Offs

A critical invariant in high-performance bare-metal co-processor design is avoiding the **"DMA-for-everything" fallacy**. Setting up a DMA transaction is NOT free.

#### The Cost Breakdown
| Transfer Mechanism | Overhead & Setup Latency | Throughput Efficiency | Best Suited For |
| :--- | :--- | :--- | :--- |
| **Direct Hardware FIFO Write** | ~2–5 CPU cycles per byte | Ideal for $\le 4$ bytes (or $\le$ FIFO depth) | Single characters, 1–2 byte I2C register access, SPI command opcodes |
| **Non-blocking ISR Ring Buffer** | ~35–50 cycles per interrupt (clearing FIFO) | Good for small bursts ($4 \dots 32$ bytes) | Interactive CLI keystrokes, sporadic GPS NMEA sentences |
| **Hardware DMA Pipeline** | ~100–160 cycles (LLI construct, cache clean/invalidate, DRQ unmask, channel arm, PLIC completion ISR context switch) | Optimal for $> 16\dots 32$ bytes (Zero CPU cycles during bus transport) | High-rate SPI sensor bursts, display buffers, bulk UART telemetry streams, I2C EEPROM dumps |

#### Why DMA for Single Characters or Short Register Access is an Anti-Pattern
* Sending a single byte `'A'` over UART via DMA requires:
  1. Constructing a 32-byte Linked-List Item in memory.
  2. Performing a data barrier / cache line clean.
  3. Writing to 6 DMA channel registers (`SRC`, `DST`, `CNT`, `PARA`, `CFG`, `EN`).
  4. Waiting for DMA bus arbitration and servicing a completion interrupt.
  * **Result**: Setting up DMA wastes $\sim 120$ clock cycles to transfer 1 byte that could have been pushed directly into `UART_THR` in a single 2-cycle store instruction!
* Similarly, for I2C: reading a 1-byte status register `WHO_AM_I` (`write 0x75 -> restart -> read 1 byte`) involves 3 bus phase transitions. Orchestrating this via multiple chained DMA descriptors adds substantial latency compared to the hardware FSM or direct FIFO write.

#### Unified Driver Threshold Balancing Policy
Peripheral drivers should dynamically select between Direct FIFO/ISR and Zero-Copy DMA based on payload size:
```
           Payload Size
               │
        ┌──────┴──────┐
        ▼             ▼
   <= THRESHOLD   > THRESHOLD
        │             │
        ▼             ▼
  Direct FIFO /    Zero-Copy DMA
  Fast-Path ISR   (LLI Descriptors)
```

1. **UART Threshold (`DMA_TX_THRESHOLD = 32` bytes)**:
   * **$\le 32$ bytes / Single Character**: Written directly to `UART_THR` if FIFO has space, otherwise enqueued into lock-free atomic SPSC ring buffer serviced by THRE interrupt.
   * **$> 32$ bytes**: Streamed through hardware DMA Channel (DRQ 16 for UART2) with coroutine suspension until complete.
2. **SPI Threshold (`DMA_THRESHOLD = 4` bytes)**:
   * **$1 \dots 4$ bytes (Command / Register access)**: Directly written to `SPI0_TXD_8`, start pulse in `SPI0_TCR`, polled or ISR-waited in under 2 $\mu$s. Avoids tying up one of the 8 precious co-processor DMA channels.
   * **$> 4$ bytes (IMU sensor burst, FPGA frames)**: Transferred via dual-channel RX/TX DMA with `AsyncTransferAwaiter`.
3. **I2C / TWI Threshold (`DMA_THRESHOLD = 32` bytes)**:
   * **$< 32$ bytes (Register read/write, sensor config)**: Executed via low-latency FSM at `0x00..0x18`.
   * **$\ge 32$ bytes (EEPROM, buffer streaming)**: Executed via Enhanced TWI Engine at `0x0200` with DMA LLI and DRQ 43.

---

## 8. FreeRTOS Peripheral Capabilities & Full API Manual

Allwinner's FreeRTOS HAL (`rtos-hal`) provides complete, bare-metal C drivers for all peripherals accessible to the E906 RISC-V and HiFi4 DSP cores.

---

### 8.1 PIO / GPIO (Digital I/O & Interrupts)

* **Physical Bases**:
  * Main SoC PIO: `0x02000000` (`SUNXI_GPIO_PBASE`)
  * Low-Power R-PIO: `0x07022000` (`SUNXI_GPIO_R_PBASE`)
* **Supported Pin Banks**: `GPIOB`, `GPIOC`, `GPIOD`, `GPIOE`, `GPIOF`, `GPIOG`, `GPIOH`, `GPIOI`, `GPIOJ`, `GPIOK`, `GPIOL`, `GPIOM`.

#### API Reference (`hal_gpio.h`)
```c
/* Configure pin direction (GPIO_DIRECTION_INPUT / GPIO_DIRECTION_OUTPUT) */
int hal_gpio_set_direction(gpio_pin_t pin, gpio_direction_t direction);

/* Configure pull resistor (GPIO_PULL_DOWN_DISABLED, GPIO_PULL_UP, etc) */
int hal_gpio_set_pull(gpio_pin_t pin, gpio_pull_status_t pull);

/* Write digital output level (GPIO_DATA_LOW = 0, GPIO_DATA_HIGH = 1) */
int hal_gpio_set_data(gpio_pin_t pin, gpio_data_t data);

/* Read digital input level */
int hal_gpio_get_data(gpio_pin_t pin, gpio_data_t *data);

/* Convert a GPIO pin to an IRQ number */
int hal_gpio_to_irq(gpio_pin_t pin, uint32_t *irq);

/* Request edge/level interrupt handler */
int hal_gpio_irq_request(
    uint32_t irq, hal_irq_handler_t handler, unsigned long flags, void *data
);

/* Enable / Disable GPIO Interrupt */
int hal_gpio_enable_irq(uint32_t irq);
int hal_gpio_disable_irq(uint32_t irq);
```

#### Production Code Example: GPIO Output & Input with Interrupt
```c
#include <hal_gpio.h>
#include <hal_log.h>

#define LED_PIN       GPIOD(18)    /* Port D, Pin 18 */
#define BUTTON_PIN    GPIOL(3)     /* Port L (R-Domain), Pin 3 */

static hal_irqreturn_t button_isr(void *data)
{
    static int led_state = 0;
    led_state = !led_state;
    hal_gpio_set_data(LED_PIN, led_state ? GPIO_DATA_HIGH : GPIO_DATA_LOW);
    hal_info("Button pressed! LED set to: %d\n", led_state);
    return HAL_IRQ_HANDLED;
}

void setup_gpio_demo(void)
{
    uint32_t button_irq;

    /* 1. Setup Output Pin */
    hal_gpio_set_direction(LED_PIN, GPIO_DIRECTION_OUTPUT);
    hal_gpio_set_pull(LED_PIN, GPIO_PULL_DOWN_DISABLED);
    hal_gpio_set_data(LED_PIN, GPIO_DATA_LOW);

    /* 2. Setup Input Pin with Falling Edge Interrupt */
    hal_gpio_set_direction(BUTTON_PIN, GPIO_DIRECTION_INPUT);
    hal_gpio_set_pull(BUTTON_PIN, GPIO_PULL_UP);

    hal_gpio_to_irq(BUTTON_PIN, &button_irq);
    hal_gpio_irq_request(button_irq, button_isr, IRQ_TYPE_EDGE_FALLING, NULL);
    hal_gpio_enable_irq(button_irq);
}
```

---

### 8.2 DMA Controller (DMA0)

* **Physical Base**: `0x03002000` (`SUNXI_DMAC_PBASE`)
* **Dedicated Core**: DMA0 is mapped to the MCU/E906 (`#define SUNXI_USED_DMA0 1`).
* **Channels**: 16 hardware channels (Channels 8..15 reserved for co-processor).
* **Interrupt**: `MAKE_IRQn(88, 0)` via Allwinner INTC Router.

#### API Reference (`hal_dma.h`)
```c
/* Request an available DMA channel */
int hal_dma_chan_request(struct sunxi_dma_chan **chan);

/* Free an allocated DMA channel */
int hal_dma_chan_free(struct sunxi_dma_chan *chan);

/* Configure slave device parameters */
int hal_dma_slave_config(
    struct sunxi_dma_chan *chan, struct dma_slave_config *config
);

/* Prepare memory-to-memory DMA descriptor */
struct dma_async_tx_descriptor *hal_dma_prep_memcpy(
    struct sunxi_dma_chan *chan,
    uint32_t dest, uint32_t src, uint32_t len
);

/* Prepare cyclic ring buffer DMA descriptor */
int hal_dma_prep_cyclic(
    struct sunxi_dma_chan *chan,
    uint32_t buf_addr, uint32_t buf_len, uint32_t period_len,
    enum dma_transfer_direction dir
);

/* Install transfer completion callback */
int hal_dma_callback_install(
    struct sunxi_dma_chan *chan,
    dma_callback callback, void *callback_param
);

/* Start / Stop transfer */
int hal_dma_start(struct sunxi_dma_chan *chan);
int hal_dma_stop(struct sunxi_dma_chan *chan);
```

#### Production Code Example: Memory-to-Memory DMA Transfer
```c
#include <hal_dma.h>
#include <hal_log.h>

static volatile int dma_done = 0;

static void dma_complete_callback(void *param)
{
    dma_done = 1;
}

int run_dma_memcpy_test(uint32_t *src, uint32_t *dst, uint32_t byte_len)
{
    struct sunxi_dma_chan *chan = NULL;
    struct dma_async_tx_descriptor *desc = NULL;

    if (hal_dma_chan_request(&chan) != HAL_DMA_STATUS_OK) {
        hal_err("Failed to allocate DMA channel!\n");
        return -1;
    }

    desc = hal_dma_prep_memcpy(chan, (uint32_t)dst, (uint32_t)src, byte_len);
    if (!desc) {
        hal_dma_chan_free(chan);
        return -2;
    }

    dma_done = 0;
    hal_dma_callback_install(chan, dma_complete_callback, NULL);
    hal_dma_start(chan);

    while (!dma_done) {
        /* Wait for DMA transfer complete interrupt */
    }

    hal_dma_stop(chan);
    hal_dma_chan_free(chan);
    return 0;
}
```

---

### 8.3 Serial / UART (10 Physical Controllers)

* **Physical Bases**:
  * `UART0` to `UART7`: `0x02500000` through `0x02501c00` (Step: `0x400`).
  * `R_UART0`: `0x07080000` (Dedicated R-Domain, CLIC line 68).
  * `R_UART1`: `0x07080400` (Dedicated R-Domain, CLIC line 69).
* **Hardware FIFO**: 64 bytes per channel.

#### API Reference (`hal_uart.h`)
```c
/* Initialize hardware UART controller */
int hal_uart_init(uart_port_t port);

/* Set baud rate (e.g. 115200, 1500000, 3000000) */
int hal_uart_set_baudrate(uart_port_t port, uint32_t baudrate);

/* Polling Single Character I/O */
void hal_uart_put_char(uart_port_t port, char c);
char hal_uart_get_char(uart_port_t port);

/* Block Transmission */
int hal_uart_send(uart_port_t port, const uint8_t *buf, uint32_t size);
int hal_uart_receive(uart_port_t port, uint8_t *buf, uint32_t size);

/* DMA Streaming */
hal_uart_status_t hal_uart_dma_start(uart_port_t uart_port);
void hal_uart_dma_stop(uart_port_t uart_port);
void hal_uart_dma_send(
    uart_port_t uart_port, const uint8_t *temp_buf, uint32_t tx_buf_len
);
```

#### Production Code Example: UART Console Driver
```c
#include <hal_uart.h>

#define CONSOLE_UART    UART_0

void console_init(void)
{
    _uart_config_t config = {
        .baudrate    = UART_BAUDRATE_115200,
        .word_length = UART_WORD_LENGTH_8,
        .stop_bit    = UART_STOP_BIT_1,
        .parity      = UART_PARITY_NONE,
    };

    hal_uart_init(CONSOLE_UART);
    hal_uart_set_format(
        CONSOLE_UART, config.word_length, config.stop_bit, config.parity
    );
    hal_uart_set_baudrate(CONSOLE_UART, config.baudrate);
}

void console_print(const char *str)
{
    while (*str) {
        if (*str == '\n')
            hal_uart_put_char(CONSOLE_UART, '\r');
        hal_uart_put_char(CONSOLE_UART, *str++);
    }
}
```

---

### 8.4 SPI Controller with Direct Hardware DMA

* **Physical Bases**:
  * `SPI0`: `0x04025000`
  * `SPI1`: `0x04026000`
  * `SPI2`: `0x04027000`
  * `R_SPI`: `0x07092000` (Dedicated R-Domain, CLIC line 76)
* **Hardware FIFOs**: 128-byte RX FIFO, 64-byte TX FIFO.
* **DMA DRQ Hardware Lines**: Mapped to `DRQDST_SPI0_TX` and `DRQSRC_SPI0_RX` on DMA0 (`DRQ = 22`).

#### API Reference (`sunxi_hal_spi.h`)
```c
/* Initialize SPI Master controller */
int hal_spi_init(spi_port_t port, hal_spi_config_t *cfg);

/* De-initialize SPI */
int hal_spi_deinit(spi_port_t port);

/* Synchronous Full-Duplex or Half-Duplex Transfer */
int hal_spi_transfer(spi_port_t port, hal_spi_master_transfer_t *transfer);

/* Polling Write / Read */
int hal_spi_write(spi_port_t port, const uint8_t *buf, uint32_t len);
int hal_spi_read(spi_port_t port, uint8_t *buf, uint32_t len);
```

#### Production Code Example: DMA-Assisted SPI Flash Read
```c
#include <sunxi_hal_spi.h>
#include <hal_log.h>

#define FLASH_SPI_PORT   0

int spi_read_flash(uint8_t cmd, uint32_t addr, uint8_t *rx_buf, uint32_t len)
{
    uint8_t tx_header[4];
    tx_header[0] = cmd;
    tx_header[1] = (addr >> 16) & 0xff;
    tx_header[2] = (addr >> 8) & 0xff;
    tx_header[3] = addr & 0xff;

    hal_spi_master_transfer_t transfer = {
        .tx_buf   = tx_header,
        .tx_len   = sizeof(tx_header),
        .rx_buf   = rx_buf,
        .rx_len   = len,
        .use_dma  = 1,              /* Activates DMA0 pipeline automatically */
    };

    return hal_spi_transfer(FLASH_SPI_PORT, &transfer);
}
```

---

### 8.5 TWI / I2C Controller

* **Physical Bases**:
  * `TWI0` to `TWI5`: `0x02502000` through `0x02503400` (Step: `0x400`).
  * `R_TWI0`: `0x07081400`
  * `R_TWI1`: `0x07081800`

#### API Reference (`sunxi_hal_twi.h`)
```c
/* Initialize TWI Port */
twi_status_t hal_twi_init(twi_port_t port);

/* Uninitialize TWI Port */
twi_status_t hal_twi_uninit(twi_port_t port);

/* Write Message Array */
twi_status_t hal_twi_write(twi_port_t port, twi_msg_t *args, uint32_t msg_num);

/* Read Registers from Slave Device */
twi_status_t hal_twi_read(
    twi_port_t port, hal_twi_transfer_cmd_t cmd,
    uint16_t slave_addr, uint8_t reg_addr, void *buf, uint32_t size
);

/* Combined Master Transfer with DMA Engine Support */
twi_status_t hal_twi_xfer(twi_port_t port, twi_msg_t *msgs, int32_t num);
```

#### Production Code Example: I2C Sensor Read
```c
#include <sunxi_hal_twi.h>
#include <hal_log.h>

#define SENSOR_I2C_PORT    TWI_MASTER_0
#define SENSOR_I2C_ADDR    0x68    /* IMU Address */

int read_sensor_whoami(uint8_t *whoami_val)
{
    hal_twi_init(SENSOR_I2C_PORT);
    twi_status_t status = hal_twi_read(
        SENSOR_I2C_PORT, I2C_SLAVE,
        SENSOR_I2C_ADDR, 0x75, whoami_val, 1
    );

    if (status != TWI_STATUS_OK) {
        hal_err("Failed to read WHO_AM_I register!\n");
        return -1;
    }
    return 0;
}
```

---

### 8.6 Hardware Spinlocks (32 Atomic Multi-Core Locks)

* **Physical Base**: `0x03005000`
* **Lock Count**: 32 independent atomic hardware registers (`num-locks = <32>`).
* **Operation**: Reading an unlocked spinlock register atomically locks it and returns `0`. Reading a locked spinlock returns `1`. Writing `0` unlocks it.

#### API Reference & Production Code Example
```c
#include <hal_hwspinlock.h>

#define SPINLOCK_SHM_ID   0    /* Lock 0 protects shared memory allocation */

void enter_critical_shared_section(void)
{
    /* Spin until lock is atomically acquired */
    while (hal_hwspinlock_check_taken(SPINLOCK_SHM_ID)) {
        /* Busy wait or yield */
    }
}

void exit_critical_shared_section(void)
{
    hal_hwspinlock_take(SPINLOCK_SHM_ID); /* Releases the lock */
}
```

---

## 9. Interrupt Controller Architecture (CLIC & INTC Router)

```text
+-----------------------------------------------------------------------+
|                 Root: T-Head CLIC (Base: 0xE0800000)                  |
|                 144 Vector Interrupts (Fast Context)                  |
+-----------------------------------------------------------------------+
      ^                ^                  ^                  ^
      | IRQ 16         | IRQ 17           | IRQ 64..79       | IRQs 90..129
+-----------+    +-----------+    +---------------+    +----------------+
| E906 WDG  |    | E906 MBOX |    | R-Domain Devs |    |  Allwinner     |
| Watchdog  |    |  Mailbox  |    | R_UART, R_SPI |    |  INTC Router   |
+-----------+    +-----------+    | R_TWI, GPIOL/M|    | (0x070210C0)   |
                                  +---------------+    +----------------+
                                                               ^
                   +-------------------------------------------+
                   |                     |                     |
             [Group 7..8]            [Group 9]          [Group 13..17]
               UART0..7               SPI0..3           GPIOA..K, DMA0
```

### 9.1 CLIC Fast Vector Table
* `IRQ 7`: Machine Core Timer (`mtime` / Tick)
* `IRQ 16`: Watchdog
* `IRQ 17`: Hardware Mailbox
* `IRQ 64 / 65`: `GPIOL` (Secure / Non-Secure)
* `IRQ 66 / 67`: `GPIOM` (Secure / Non-Secure)
* `IRQ 68 / 69`: `R_UART0` / `R_UART1`
* `IRQ 70 / 71`: `R_TWI0` / `R_TWI1`
* `IRQ 76`: `R_SPI`
* `IRQ 88` (routed): `DMA0_CPUX_NS` / MCU DMA interrupt

---

---

## 10. Memory Architecture, DDR Partitioning & E906 L1 Cache Management

```text
+-----------------------+ 0x4a000000 (Physical DDR)
| DSP Firmware Window   | 10 MB (DSP Virtual: 0x3a000000)
|  - .version_table     | Offset 0x000 (0x3a000000)
|  - .resource_table    | Offset 0x100 (0x3a000100) <-- RemoteProc Res Table
|  - .text, .data, HEAP | Offset 0x200..
+-----------------------+ 0x4ac00000
| OpenAMP VDEV Buffer   | 1 MB (Shared DMA Buffer Pool for RPMsg Payload)
+-----------------------+ 0x4ad00000
| Vring 0 (RX Ring)     | 16 KB (VirtIO Descriptors, Avail, Used)
+-----------------------+ 0x4ad04000
| Vring 1 (TX Ring)     | 16 KB (VirtIO Descriptors, Avail, Used)
+-----------------------+ 0x60000000 (Physical DDR Carveout)
| RISC-V Firmware Window| 10 MB (E906 DDR Code & Data Segment)
+-----------------------+
```

### 10.1 Why FreeRTOS in the Tina SDK is Loaded into DDR

A frequent question when analyzing the Tina SDK is: *Why does FreeRTOS run out of external DDR (`0x60000000` or `0x48100000`) instead of internal on-chip SRAM?*

1. **Massive Middleware Footprint in Tina FreeRTOS**:
   * The Allwinner Tina FreeRTOS distribution (`melis4.0` / Tina RTOS Core) is not a stripped-down bare-metal scheduler. It is shipped as a full-featured IoT operating system bundle containing:
     * **LwIP Network Stack**: Full TCP/IP, UDP, DHCP, DNS, sockets, and Wi-Fi/Ethernet MAC drivers.
     * **OpenAMP & RPMsg Framework**: Complete VirtIO device layers, dynamic vring parsers, and endpoint multiplexers.
     * **Virtual File System (VFS)**: POSIX-like filesystem layer, FATFS / LittleFS, block layer drivers.
     * **Audio Subsystem**: Audio framework, decoders, software mixers, and stream buffers.
     * **Diagnostic CLI / Shell**: Interactive command-line shell (Finsh / MSH), symbol table dumps, and RTOS trace loggers.
     * **Multi-Megabyte Dynamic Heaps**: Substantial heap space allocated for network packet buffers, audio PCM frames, and dynamic RTOS tasks.
   * As a result, a compiled Tina FreeRTOS ELF (`rtos.elf`) routinely spans **2 MB to 8 MB**, making it physically impossible to fit in on-chip SRAM.

2. **On-Chip SRAM Resource Scarcity & Firewalls**:
   * The SoC provides limited on-chip SRAM blocks, heavily partitioned across security and hardware domains:
     * `0x00020000` (128 KB): Hard-wired local SRAM dedicated to the **HiFi4 DSP**.
     * `0x00044000` (160 KB): Hard-wired **OP-TEE / TrustZone (SRAM A2)**; firewalled for secure world operations.
     * `0x3FFC0000` (256 KB) & `0x40000000` (256 KB via remap): Co-processor SRAM (SRAM Space 0 & Space 1). The absolute maximum on-chip SRAM capacity available is **512 KB**.
   * Because 512 KB cannot accommodate the Tina FreeRTOS image, Tina Linux reserves a dedicated **10 MB+ carveout in DRAM** via Devicetree (`reserved-memory` node with `no-map`).
   * When Linux launches the E906 co-processor via `remoteproc`, it loads the ELF into this DDR window, writes the DDR entry vector (e.g. `0x60000000`) into `E906_STA_ADD_REG` (`0x07130204`), and deasserts reset.

---

### 10.2 XuanTie E906 L1 Cache Subsystem (ICache & DCache)

Because the E906 core executes code and accesses data across the external DDR bus fabric, **L1 Cache is mandatory**. 

#### The Latency Problem
* An un-cached fetch or load/store transaction across the SoC AXI interconnect and DDR controller incurs **50 to 120 CPU clock cycles** of wait-state latency.
* Running without cache drops the effective performance of the 200 MHz core by ~80% to 90%.
* Enabling the L1 Instruction Cache (ICache) and Data Cache (DCache) achieves an execution efficiency approaching **1 Cycle Per Instruction (CPI)** for cached inner loops.

#### E906 L1 Cache Hardware Parameters
* **Instruction Cache (ICache)**: 16 KB or 32 KB, 2-way set-associative, **32-byte cache line size**, FIFO replacement policy.
* **Data Cache (DCache)**: 16 KB or 32 KB, 2-way set-associative, **32-byte cache line size**, **Write-Back with Write-Allocate (WB/WA)** policy.

#### T-Head Vendor Machine-Mode CSRs for Cache Control
Standard RISC-V originally omitted standardized cache management instructions. The Alibaba T-Head XuanTie architecture provides dedicated Machine-mode CSRs:

| CSR Address | Register Name | Description | Key Bitfields |
| :--- | :--- | :--- | :--- |
| `0x7C1` | **`mhcr`** | Machine Hardware Control Register | Bit 0: `IE` (ICache Enable)<br>Bit 1: `DE` (DCache Enable)<br>Bit 2: `WA` (Write-Allocate Enable)<br>Bit 3: `WB` (Write-Back Enable)<br>Bit 4: `RS` (Return Stack Enable)<br>Bit 5: `BPE` (Branch Prediction Enable)<br>Bit 8: `L0BTB` (Branch Target Buffer) |
| `0x7C2` | **`mcor`** | Machine Cache Operation Register | Bits [1:0]: Flush/Invalidate all ICache/DCache (`0x03` = Invalidate All) |
| `0x7C3` | **`mcrr`** | Machine Cache Range Register | Physical address range pointer for line-selective cache maintenance |

#### FreeRTOS Cache Startup Flow (`crt0.S` / `start.S`)
During co-processor reset initialization, the assembly startup code configures the cache subsystem before jumping to the C runtime:

```assembly
    /* 1. Invalidate both I-Cache and D-Cache prior to activation */
    li   t0, 0x03               /* Invalidate all ICache & DCache */
    csrw 0x7c2, t0              /* Write to mcor (0x7C2) */
    fence.i                     /* Synchronize instruction fetch pipeline */

    /* 2. Enable Caches, Write-Back, Write-Allocate, and Branch Prediction */
    li   t0, (1 << 0) |         /* IE:  Instruction Cache Enable */ \
             (1 << 1) |         /* DE:  Data Cache Enable */ \
             (1 << 2) |         /* WA:  Write Allocate */ \
             (1 << 3) |         /* WB:  Write Back */ \
             (1 << 5)           /* BPE: Branch Prediction Enable */
    csrw 0x7c1, t0              /* Write to mhcr (0x7C1) */
```

---

### 10.3 Cache Coherency & DMA Invariants (The Non-Coherent Hazard)

The XuanTie E906 core is **NOT hardware cache-coherent** with external bus masters (the Allwinner DMA0 engine, Cryptographic accelerator, or ARM Cortex-A55 Linux host).

When FreeRTOS operates with D-Cache enabled on DDR, software **MUST** strictly manage cache coherency around all DMA operations:

#### 1. CPU-to-DMA (Transmit / Peripheral Write)
* **The Hazard**: The CPU writes data into a DDR buffer, but the modified data remains dirty inside the E906 L1 D-Cache lines. The DMA engine accesses physical DDR directly and transfers stale data.
* **Mandatory Action**: The CPU must execute a **D-Cache Clean / Flush** (`hal_dcache_clean(addr, size)`) before initiating the DMA transaction:
  ```c
  hal_dcache_clean((uint32_t)tx_buffer, buffer_len);
  hal_dma_start(dma_chan);
  ```

#### 2. DMA-to-CPU (Receive / Peripheral Read)
* **The Hazard**: The DMA engine writes received bytes into physical DDR. If the E906 core previously speculative-fetched or loaded that memory range into its L1 D-Cache, subsequent CPU reads will return old cached values.
* **Mandatory Action**: The CPU must execute a **D-Cache Invalidate** (`hal_dcache_invalidate(addr, size)`) after DMA completion before reading the buffer:
  ```c
  /* In DMA completion ISR or after polling complete */
  hal_dcache_invalidate((uint32_t)rx_buffer, buffer_len);
  process_received_data(rx_buffer, buffer_len);
  ```

#### 3. Strict 32-Byte Alignment & Padding Requirement
* Because cache lines are **32 bytes wide**, any DMA buffer that undergoes cache invalidation **MUST be 32-byte aligned** and sized to a multiple of 32 bytes:
  ```c
  __attribute__((aligned(32))) uint8_t dma_rx_buffer[64];
  ```
* **False Sharing Trap**: If an unaligned 10-byte buffer shares a 32-byte cache line with adjacent RTOS kernel variables, invalidating the buffer will instantly corrupt the neighbouring variables!

#### 4. Can Memory Regions Be Marked Non-Cacheable via PMP or SYSMAP?
A common misconception is that the E906 (or E907) can configure physical memory protection (PMP) or memory windows to make specific RAM ranges (e.g. DDR VirtIO vrings or shared SRAM) **non-cacheable** to eliminate cache maintenance.

* **Standard RISC-V PMP Limitation**:
  * In the RISC-V Privileged Architecture, PMP registers (`pmpcfg0`–`pmpcfg3` / `pmpaddr0`–`pmpaddr15`) control **access permissions only**:
    * `PMP_R` (`0x01`): Read permission
    * `PMP_W` (`0x02`): Write permission
    * `PMP_X` (`0x04`): Execute permission
    * Address matching modes: `OFF`, `TOR`, `NA4`, `NAPOT`.
  * **PMP has zero bits for cacheability or bufferability.** Writing PMP registers cannot mark a DDR or SRAM region as non-cacheable.
* **T-Head SYSMAP Hardware Attribute Architecture**:
  * The XuanTie E906 core architecture uses an internal **SYSMAP** (System
    Memory Map) unit to govern Physical Memory Attributes (**C** = Cacheable,
    **B** = Bufferable, **SO** = Strongly Ordered).
  * **Hardwired in Silicon at ASIC Synthesis**: On the microcontroller E906
    integrated into Allwinner T527/A523 silicon, the SYSMAP region descriptors
    are **completely hardwired into the silicon gates** (there is no runtime
    MAEE or MMU page table translation; Tina SDK's `mmu.c` literally notes:
    *"It's a fake mmu, just a e906 perspective translation"*).
  * **Fixed Hardware Routing**:
    * Peripheral MMIO space (`0x00000000` - `0x3FFFFFFF` except SRAM) is
      hardwired as **Strongly Ordered / Non-cacheable**.
    * Internal on-chip SRAM (`0x3FFC0000` / `0x40000000`) and external DDR DRAM
      (`0x40000000`+) are hardwired as **Normal Cacheable Memory**.
  * **The Architectural Consequence**:
    * There are **no runtime CSR-programmable PMA registers** on E906 to
      dynamically carve out non-cacheable RAM windows inside DDR or SRAM.
    * Software CANNOT change a RAM region's cacheability at runtime.
    * When D-Cache is enabled in `mhcr` (`DE = 1`), **all normal RAM accesses
      unconditionally pass through the L1 D-Cache**.
    * When D-Cache is disabled in `mhcr` (`DE = 0`), **all RAM accesses bypass
      the cache entirely**, hitting the AXI bus directly.
* **Proof from Allwinner Tina SDK (`rtos-components/thirdparty/openamp/`)**:
  Because memory cannot be marked non-cacheable at runtime, Allwinner’s official OpenAMP and VirtIO drivers **must and do explicitly perform software cache maintenance** using T-Head custom instructions before and after every buffer exchange:
  * Prior to transmitting an RPMsg buffer or descriptor: `hal_dcache_clean((unsigned long)addr, len);`
  * Prior to reading a received RPMsg buffer or vring: `hal_dcache_invalidate((unsigned long)addr, len);`

#### 5. Official T-Head Cache Maintenance Instructions & Raw Opcodes
Because standard RISC-V originally lacked dedicated cache operations, the XuanTie E906 implements custom instructions mapped in the vendor opcode space (`0x0B`). The Tina SDK implementation (`arch/riscv/e90x/cache.c`) provides:

```c
#define L1_CACHE_BYTES (32)

/* 1. Data Cache Write-Back (Clean) Range by Physical Address */
static void dcache_wb_range(unsigned long start, unsigned long end) {
    register unsigned long i asm("a5");
    i = start & ~(L1_CACHE_BYTES - 1);
    for (; i < end; i += L1_CACHE_BYTES) {
        asm volatile(".word 0x0297800b\n" ::: "memory"); // dcache.cpa a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");       // fence
}

/* 2. Data Cache Invalidate Range by Physical Address */
static void dcache_inv_range(unsigned long start, unsigned long end) {
    register unsigned long i asm("a5");
    i = start & ~(L1_CACHE_BYTES - 1);
    for (; i < end; i += L1_CACHE_BYTES) {
        asm volatile(".word 0x02a7800b\n" ::: "memory"); // dcache.iva a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");       // fence
}

/* 3. Data Cache Clean & Invalidate Range by Physical Address */
static void dcache_wbinv_range(unsigned long start, unsigned long end) {
    register unsigned long i asm("a5");
    i = start & ~(L1_CACHE_BYTES - 1);
    for (; i < end; i += L1_CACHE_BYTES) {
        asm volatile(".word 0x02b7800b\n" ::: "memory"); // dcache.civa a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");       // fence
}

/* 4. Global Whole-Cache Operations */
void FlushDcacheAll(void) {
    asm volatile(".word 0x0010000b" ::: "memory"); // dcache.call (clean all)
}

void InvalidDcache(void) {
    asm volatile(".word 0x0020000b" ::: "memory"); // dcache.iall (inval all)
}

void CleanFlushDcacheAll(void) {
    asm volatile(".word 0x0030000b" ::: "memory"); // dcache.ciall (clean&inval)
}

void FlushIcacheAll(void) {
    asm volatile(".word 0x0100000b" ::: "memory"); // icache.iall (inval icache)
}
```

---

### 10.4 Is On-Chip SRAM Non-Cacheable? (DMA Coherency on SRAM vs DDR)

A critical architectural question is: *Are on-chip SRAM regions (`0x3FFC0000` / `0x40000000`) non-cacheable by hardware, and does DMA in SRAM require cache maintenance?*

#### 1. Silicon & Tina SDK Ground Truth
In Allwinner's official Tina OpenAMP memory mapping table (`rtos-components/thirdparty/openamp/sunxi_helper/sunxi/sun55iw3/memory.c`):

```c
const struct mem_mapping mem_mappings[] = {
    /* Low peripheral / DSP space: Non-Cacheable */
    { .va = 0x00020000, .len = 0x20000, .pa = 0x00020000,
      .attr = MEM_NONCACHEABLE }, /* HiFi4 DSP RAM */
    { .va = 0x00040000, .len = 0x24000, .pa = 0x00040000,
      .attr = MEM_NONCACHEABLE }, /* OP-TEE SRAM A2 */

    /* E906 Dedicated On-Chip SRAM: CACHEABLE! */
    { .va = 0x3ffc0000, .len = 0x40000, .pa = 0x07280000,
      .attr = MEM_CACHEABLE },    /* SRAM Space 0 */
    { .va = 0x40000000, .len = 0x40000, .pa = 0x072c0000,
      .attr = MEM_CACHEABLE },    /* SRAM Space 1 */

    /* External DDR DRAM: CACHEABLE! */
    { .va = 0x40040000, .len = 0x3ffc0000, .pa = 0x40040000,
      .attr = MEM_CACHEABLE },    /* DDR DRAM */
};
```

1. **On-Chip SRAM is NOT Hardware Non-Cacheable**:
   * SRAM Space 0 and Space 1 reside in the normal memory address space.
   * When L1 D-Cache is enabled in `mhcr` (`DE = 1`), CPU reads and writes to `0x3FFC0000` are allocated into the L1 D-Cache lines.
   * **If DCache is enabled (`DE = 1`), DMA to/from SRAM STILL requires cache maintenance** (`dcache.cpa` write-back before DMA TX, `dcache.iva` invalidate after DMA RX).

2. **Why Low Addresses (`< 0x38000000`) Are Marked Non-Cacheable**:
   * `0x00020000` is dedicated to the HiFi4 DSP (E906 cannot use it).
   * `0x00044000` is OP-TEE / TrustZone Secure SRAM A2 (firewalled from Linux / FreeRTOS non-secure world).
   * E906's actual SRAM lives strictly at `0x3FFC0000` and `0x40000000`.

#### 2. The Two Operating Paradigms

| Operating Mode | Cache Configuration | SRAM Performance | DMA Maintenance Overhead | Best Suited For |
| :--- | :--- | :--- | :--- | :--- |
| **Tina FreeRTOS Mode** | `mhcr.IE = 1`<br>`mhcr.DE = 1` | Full L1 Cache on both SRAM & DDR | **Mandatory on all DMA & IPC buffers** (must call `hal_dcache_clean` / `invalidate`) | Large IoT stacks (LwIP, Audio, Filesystems) executing out of external DDR |
| **Bare-Metal SRAM Mode** | `mhcr.IE = 1`<br>`mhcr.DE = 0` | Near single-cycle direct SRAM access (no wait states) | **ZERO maintenance required** (CPU and DMA0 touch physical SRAM cells directly) | Hard real-time sensor loops, motor control, lock-free ring buffers in on-chip SRAM |

* In bare-metal firmware (`riscv-firmware`), operating with **DCache disabled (`DE = 0`) and ICache enabled (`IE = 1`)** guarantees 100% hardware DMA coherency without any cache flushes, while retaining single-cycle instruction execution speed from ICache.

---

## 11. Summary Checklist for Upstream RemoteProc & Co-Processor Drivers

1. **Silicon Identity**: Refer strictly to **T-Head XuanTie E906** (`RV32IMAFCX`).
2. **Clock Tree Safety**:
   * Linux Host CCU owns all PLLs.
   * Co-processor firmware controls only internal peripheral dividers (`DLL`/`DLH`, `SPI_CCR`, `TWI_CCR`).
3. **High-Performance I/O**:
   * Use DMA0 Channels 8..15 for UART, SPI, and TWI.
   * Configure cyclic ring buffers for UART and scatter-gather descriptors for SPI to achieve zero CPU load.
4. **DSP Resource Table**:
   * Always place the remoteproc resource table at **offset `0x100`** (`0x4a000100` physical / `0x3a000100` virtual) for upstream Linux compatibility.
