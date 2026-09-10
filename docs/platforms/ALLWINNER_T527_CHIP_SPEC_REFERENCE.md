# Allwinner T527 / A527 (sun55i / sun60iw1) SoC Hardware Architecture & Chip Specification Reference

> **Document Status**: Authoritative Reference Compiled from:
> - *Allwinner T527 User Manual V0.92* (1,823 pages)
> - *Allwinner T527 Datasheet V0.91*
> - *Vendor BSP (AW2501 / Linux 5.15, `sun55iw3p1.dtsi`, `sun60iw1p1.dtsi`)*
> - *Mainline Linux 7.1 RemoteProc (`sunxi_rproc.c`)*
> 
> **Target Board**: Radxa Cubie A5E (Allwinner A527 / T527)  
> **Local User Manual Path**: `/run/media/tcmichals/projects/radxa/docs/T527/Hardware硬件类文档/芯片手册/T527_User_Manual_V0.92.pdf`

---

## 1. SoC Overview & Heterogeneous Architecture

The Allwinner T527 / A527 is an advanced, heterogeneous octa-core ARM application processor integrated with two specialized auxiliary co-processors designed for commercial, automotive, robotics, avionics, and edge AI compute.

```
+---------------------------------------------------------------------------------------------------+
|                                     ALLWINNER T527 / A527 SOC                                     |
|                                                                                                   |
|  +---------------------------------------+   +-------------------------------------------------+  |
|  |           CPUX DOMAIN (ARM64)         |   |          MCU SUBSYSTEM (MCU_SYS DOMAIN)         |  |
|  |  +---------------------------------+  |   |  +-------------------------------------------+  |  |
|  |  | 4x ARM Cortex-A55 @ 1.8 GHz     |  |   |  | XuanTie E906/E907 32-bit RISC-V Core      |  |  |
|  |  | L1: 32KB I / 32KB D, L2: 128KB   |  |   |  | @ up to 200 MHz (RV32IMAFDC + DSP/RVP)    |  |  |
|  |  +---------------------------------+  |   |  | (Avionics, low-jitter control, RT tasks)   |  |  |
|  |  +---------------------------------+  |   |  +-------------------------------------------+  |  |
|  |  | 4x ARM Cortex-A55 @ 1.8 GHz     |  |   |  +-------------------------------------------+  |  |
|  |  | L1: 32KB I / 32KB D, L2: 64KB    |  |   |  | Cadence Tensilica HiFi4 Audio DSP         |  |  |
|  |  +---------------------------------+  |   |  | @ 600 MHz (Audio processing, vector math) |  |  |
|  |  | DynamIQ Shared Unit (DSU)       |  |   |  | - 64 KB IRAM + 2x 32 KB DRAM Local Memory  |  |  |
|  |  | L3 Cache: 512 KB                |  |   |  +-------------------------------------------+  |  |
|  |  +---------------------------------+  |   |  - MCU CCU / PRCM Control & Resets              |  |
|  +---------------------------------------+   +-------------------------------------------------+  |
|                                                                                                   |
|  +---------------------------------------------------------------------------------------------+  |
|  |                     ON-CHIP SRAM & HARDWARE MEMORY REMAP INTERCONNECT                       |  |
|  |  - 128 KB Boot ROM (BROM @ 0x00000000)                                                      |  |
|  |  - 128 KB HiFi4 DSP Local RAM (PubSRAM C @ 0x00020000) [DSP Local Instruction/Data Memory]    |  |
|  |  - 160 KB Secure SRAM A2 (0x00044000) [TF-A BL31 / OP-TEE / PSCI 1.1]                       |  |
|  |  - 128 KB DSP Secondary Local Memory (0x00400000 IRAM, 0x00420000/0x00440000 DRAM)          |  |
|  |  - 512–1024 KB Dual-Bank SRAM A3 (Exclusive E907 RISC-V Firmware Space):                   |  |
|  |    * SRAM A3_1 (Space 0): 256/512 KB @ 0x07280000/0x07200000 (Core DA 0x40000000)          |  |
|  |    * SRAM A3_2 (Space 1): 256/512 KB @ 0x072c0000/0x07280000 (Core DA 0x40040000)          |  |
|  |      --> Switchable via REMAP_CTRL_REG (Offset 0x364, Bit 1) into MCU_SYS                   |  |
|  +---------------------------------------------------------------------------------------------+  |
|                                                                                                   |
|  +---------------------------------------+   +-------------------------------------------------+  |
|  |            AI & GRAPHICS              |   |                STORAGE & HIGH-SPEED IO          |  |
|  |  - 2.0 TOPS VeriSilicon VIP9000 NPU   |   |  - 1x PCIe 2.1 (Gen2 x1 lane, RC & EP modes)    |  |
|  |  - ARM Mali-G57 MC1 3D GPU            |   |  - 1x USB 3.1 Gen1 / PCIe Combo PHY             |  |
|  |  - Allwinner SmartColor Display Engine|   |  - 1x USB 2.0 DRD (OTG) + 1x USB 2.0 Host       |  |
|  |  - H.264/H.265/AVS2 4K@60fps VPU      |   |  - Dual 1 Gbps GMAC (RGMII / RMII)              |  |
|  |  - 4-Channel MIPI CSI-2 + ISP (16 MP) |   |  - 3x SD/MMC Controllers (SMHC0/1/2)           |  |
|  +---------------------------------------+   +-------------------------------------------------+  |
+---------------------------------------------------------------------------------------------------+
```

---

## 2. On-Chip SRAM Topology & Physical Partitioning

The Allwinner T527 features multiple discrete physical SRAM pools distributed across different clock and power domains:

| SRAM Pool Name | Physical Address (ARM CPUX) | Size | Domain Owner | Hardware Access & Usage | Allowed for E907? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`BROM`** | `0x00000000`–`0x0001FFFF` | 128 KB | SoC HW | Silicon Mask ROM; executes first power-on boot instruction | ❌ **No** (BootROM) |
| **`DSP Local RAM` (`PubSRAM C`)** | `0x00020000`–`0x0003FFFF` | 128 KB | Cadence HiFi4 DSP | Physically wired to HiFi4 DSP as local Instruction/Data RAM. Host peeks via `REMAP[0]=1`. | ❌ **FORBIDDEN (DSP Collision)** |
| **`SRAM A2`** | `0x00044000`–`0x00067FFF` | 160 KB | Secure World | TF-A BL31, OP-TEE, PSCI 1.1 CPU power management. Hardware firewalled. | ❌ **FORBIDDEN (TrustZone)** |
| **`DSP Local IRAM`** | `0x00400000`–`0x0040FFFF` | 64 KB | DSP / System | HiFi4 Instruction RAM | ❌ **FORBIDDEN (DSP Local)** |
| **`DSP Local DRAM0`**| `0x00420000`–`0x00427FFF` | 32 KB | DSP / System | HiFi4 Data RAM Bank 0 | ❌ **FORBIDDEN (DSP Local)** |
| **`DSP Local DRAM1`**| `0x00440000`–`0x00447FFF` | 32 KB | DSP / System | HiFi4 Data RAM Bank 1 | ❌ **FORBIDDEN (DSP Local)** |
| **`SRAM A3_1` (Space 0)** | `0x07280000` / `0x07200000` | 256/512 KB | `MCU_SYS` (E907) | Zero-wait-state MCU SRAM Space 0 (**E907 Core DA `0x40000000`**). Primary firmware pool. | ✅ **YES (Primary E907 Pool)** |
| **`SRAM A3_2` (Space 1)** | `0x072c0000` / `0x07280000` | 256/512 KB | Shared / System | Zero-wait-state MCU SRAM Space 1 (**E907 Core DA `0x40040000`**). Accessible when `REMAP[1] = 1`. | ✅ **YES (Secondary E907 Pool)** |

> [!NOTE]
> On the A527 variant (`sun55iw3p1`), `SRAM A3` is partitioned in the device tree as two 256 KB slices:
> - `r_sram` @ `0x07280000` (256 KB) $\rightarrow$ Core DA `0x40000000`
> - `r_sram1` @ `0x072c0000` (256 KB) $\rightarrow$ Core DA `0x40040000`
> 
> On the T527 variant (`sun60iw1p1`), `SRAM A3` is partitioned as two 512 KB slices:
> - `r_sram` @ `0x07200000` (512 KB) $\rightarrow$ Core DA `0x40000000`
> - `r_sram1` @ `0x07280000` (512 KB) $\rightarrow$ Core DA `0x40040000`

> [!IMPORTANT]
> ### HARDWARE TRUTH: NO TCM & NO 0x00020000 FOR E907
> 1. **Zero TCM in Silicon**: Unlike older chips (D1/V853), XuanTie E907 on T527 implements **NO ITCM and NO DTCM**. Addresses `0x00000000` and `0x00080000` do not exist.
> 2. **0x00020000 is HiFi4 DSP Memory**: Silicon is wired directly to the Cadence HiFi4 DSP as its local Instruction/Data RAM. E907 execution here causes fatal bus collisions with the DSP.
> 3. **0x00044000 is OP-TEE / TrustZone**: Firewalled for secure boot and OP-TEE.
> 4. **E907 Belongs Exclusively in SRAM_A3**:
>    - Primary pool: `0x40000000` (`r_sram`, 256–512 KB)
>    - Secondary pool: `0x40040000` (`r_sram1`, 256–512 KB via `REMAP_CTRL_REG[1] = 1`)
>    - DDR Carveouts: `0x48100000` for streaming VirtIO RPMsg payload buffers.

---

## 3. Hardware Remap Control Register (`REMAP_CTRL_REG`)

The interconnection and sharing of memory between the main system bus (ARM Cortex-A55) and the co-processor domain (`MCU_SYS`) is governed by the **`REMAP_CTRL_REG`**:

### Register Specification

* **Register Name**: `REMAP_CTRL_REG`
* **Register Offset**: `0x0364`
* **Physical Base Address**:
  * **T527 / `sun60iw1`**: `0x07140000` (`dsp_ccu`) $\rightarrow$ **`0x07140364`**
  * **A527 / `sun55iw3`**: `0x07010000` (`r_ccu` / `PRCM`) $\rightarrow$ **`0x07010364`**
* **Devicetree Resource Name**: `"remap"` or `"sram-for-cpux"`

```
  31                                             2   1   0
+-------------------------------------------------+---+---+
|                    RESERVED                     | B | A |
+-------------------------------------------------+---+---+
                                                    │   │
  Bit 1: SRAMA3_2_RAM_REMAP ────────────────────────┘   │
  Bit 0: MCU_RAM_REMAP ─────────────────────────────────┘
```

### Bitfield Descriptions

| Bit | Field Name | Type | Reset | Description & Operational Behavior |
| :---: | :--- | :---: | :---: | :--- |
| **31:2**| *Reserved* | R | `0x0` | Reserved. Reads undefined, write as zero. |
| **1** | **`SRAMA3_2_RAM_REMAP`** | R/W | `0x0` | **SRAM A3 Partition 2 Sharing Control**:<br>• **`0`**: `SRAMA3_2` does **not** bridge for `MCU_SYS`.<br>• **`1`**: `SRAMA3_2` **bridges for `MCU_SYS`**, exposing the secondary SRAM_A3 bank at Core DA **`0x40040000`** for high-speed E907 execution/IPC. |
| **0** | **`MCU_RAM_REMAP`** | R/W | `0x1` | **DSP Local Memory Sharing Control**:<br>• **`0`**: DSP local memory (`0x00020000` / `0x00400000`) is **only for HiFi4 DSP**.<br>• **`1`**: DSP memory window is bridged into the host address map so ARM CPUX (Linux kernel) can peek in to send IPC messages to the DSP. *(E907 leaves this as 0).* |

---

## 4. Multi-Core Address Space Translation (Tri-Core View)

Because the ARM Cortex-A55, XuanTie RISC-V, and Tensilica HiFi4 DSP connect to the memory fabric through distinct bus interfaces, addresses translate across core boundaries:

```
+===================================================================================================+
|                             T527 HETEROGENEOUS MEMORY TRANSLATION MATRIX                          |
+===================================================================================================+

    ARM64 CPUX HOST VIEW              XUANTIE RISC-V CORE VIEW           HIFI4 AUDIO DSP CORE VIEW
    (Physical Addresses)              (Device Addresses - DA)            (Device Addresses - DA)
    ====================              =======================            =======================
    
    0x00020000 [ 128 KB ] ──────────> [ FORBIDDEN TO E907 ] ───────────> 0x00020000 (DSP Local RAM)
      (HiFi4 DSP RAM / PubSRAM C)       (Collision with DSP!)              (Internal Instruction/Data RAM)

    0x00044000 [ 160 KB ] ──────────> [ FORBIDDEN TO E907 ] ───────────> [ FORBIDDEN TO DSP ]
      (Secure SRAM A2 / OP-TEE)         (TrustZone Firewall)               (TrustZone Firewall)

    0x07280000 [ 256/512 KB ] ──────> 0x40000000 (SRAM_A3 Space 0) ───> 0x07280000 (Shared Window)
      (SRAM A3 Slice 0 / r_sram)        (Primary E907 Boot & Code)         (Secondary Window)

    0x072C0000 [ 256/512 KB ] ──────> 0x40040000 (SRAM_A3 Space 1) ───> 0x072C0000 (Shared Window)
      (SRAM A3 Slice 1 / r_sram1)       (SRAMA3_2 via REMAP[1]=1)          (SRAMA3_2 Bank)

    0x48100000 [ 1 MB ] ────────────> 0x48100000 (DDR DRAM Carveout) ──> 0x48100000 (DDR Carveout)
      (DDR Non-cacheable Pool)          (PMP DMA Payload Pool)             (Audio Buffer Carveout)
+===================================================================================================+
```

---

## 5. Control, Peripheral & Inter-Core Registers

### 5.1 XuanTie RISC-V Configuration Block (`0x07130000`)

| Offset | Register Name | Description | Reset | Hardware Usage |
| :--- | :--- | :--- | :---: | :--- |
| `0x0000` | `E906_VER_REG` | IP Core Version | `0x00000001` | Identifies silicon revision |
| `0x0010` | `E906_RF1P_CFG_REG` | Control Register 0 | `0x00000000` | Memory & pipeline timing configuration |
| `0x0040` | `E906_TS_TMODE_SEL`| Test Mode Select | `0x00000000` | JTAG / BIST test mode selection |
| **`0x0204`** | **`E906_STA_ADD_REG`**| **Boot Entry Vector** | `0x00000000` | **Initial PC vector written by RemoteProc prior to un-reset** |
| `0x0220` | `E906_WAKEUP_EN_REG`| Wakeup Enable | `0x00000000` | Standby low-power wakeup enable |
| `0x0248` | `E906_WORK_MODE_REG`| Work Mode & Status | `0x00000003` | Core status: Bit 3 = `BIT_LOCK_STA` (Hardware lockup indicator) |

### 5.2 HiFi4 DSP Configuration Block (`0x07300000` / `0x07100000`)

| Offset | Register Name | Description | Hardware Usage |
| :--- | :--- | :--- | :--- |
| `0x0000` | `HIFI4_ALT_RESET_VEC` | DSP Reset Vector | HiFi4 startup PC entry point |
| `0x0004` | `HIFI4_CTRL_REG0` | DSP Control Register 0 | Bit 0: `RUN_STALL`, Bit 1: `START_VEC_SEL`, Bit 2: `HIFI4_CLKEN` |
| `0x000C` | `HIFI4_PRID_REG` | Processor ID | Tensilica architectural ID |
| `0x0010` | `HIFI4_STAT_REG` | Execution Status | Run / stall / interrupt pending flags |

### 5.3 Hardware Message Box Channels (`0x03004000` / `0x03003000`)

The central mailbox controller routes doorbells and 32-bit data words between the 3 processing domains:

| Channel Range | Source Domain | Destination Domain | Host Interrupt | Co-Processor Interrupt |
| :--- | :--- | :--- | :--- | :--- |
| **Channels 0–3** | ARM CPUX | CPUS (Power Core) | GIC SPI 0 | CPUS IRQ 12 |
| **Channels 4–7** | ARM CPUX | Cadence HiFi4 DSP | GIC SPI 355 / 181 | DSP IRQ 7 |
| **Channels 8–11**| ARM CPUX | XuanTie RISC-V | GIC SPI 345 / 174 | PLIC IRQ 25 / Core Local IRQ |

---

## 6. Linux RemoteProc Hardware Integration Flow

To utilize `SRAMA3_2` and boot firmware safely, the Linux RemoteProc driver (`sunxi_rproc.c`) executes the following hardware lifecycle:

```text
================================================================================
                    REMOTEPROC DRIVER LIFECYCLE WITH REMAP
================================================================================

1. DRIVER PROBE:
   ├── Map "cfg" register block (0x07130000)
   ├── Map "r_sram" memory window (0x07280000 / 0x07200000 - SRAM_A3 Space 0)
   ├── Map "r_sram1" memory window (0x072c0000 / 0x07280000 - SRAM_A3 Space 1 / SRAMA3_2)
   └── Map "remap" control register (0x07010364 / 0x07140364)

2. CORE PREPARATION (sunxi_rproc_prepare):
   ├── Deassert CCU bus & configuration resets
   ├── Enable CCU module clocks (bus, core)
   ├── CONFIGURE HARDWARE REMAP:
   │   ├── Read REMAP_CTRL_REG (offset 0x364)
   │   ├── Set Bit 1 (SRAMA3_2_RAM_REMAP = 1) -> Connects SRAMA3_2 to MCU_SYS!
   │   └── Write back REMAP_CTRL_REG (Bit 0 left untouched for DSP)
   └── Cleanly zero out SRAM memory banks (memset_io) to eliminate parity noise

3. ELF SEGMENT LOADING (sunxi_rproc_da_to_va):
   ├── DA 0x40000000..0x4003FFFF ─────────> Copies to SRAM_A3 Space 0 (0x07280000 / 0x07200000)
   ├── DA 0x40040000..0x4007FFFF ─────────> Copies to SRAM_A3 Space 1 (0x072c0000 / 0x07280000)
   ├── DA < 0x00020000 (BROM)    ─────────> REJECTED (-EINVAL)
   ├── DA 0x00020000..0x0003FFFF (DSP) ───> REJECTED (-EINVAL)
   ├── DA 0x00040000..0x00067FFF (OP-TEE)-> REJECTED (-EINVAL)
   └── DA 0x48100000+            ─────────> Copies to DDR DMA Reserved Memory Pool

4. CORE LAUNCH (sunxi_rproc_start):
   ├── Write ELF Entry Address (0x40000000) to E906_STA_ADD_REG (0x07130204)
   └── Deassert Core Reset (rst_core) -> XuanTie RISC-V begins execution!
================================================================================
```

---

## 7. Device Tree Specification (Mainline Reference)

### T527 / A527 Reference DTS Node
```dts
hifi4_rproc: hifi4_rproc@7140364 {
	compatible = "allwinner,hifi4-rproc";
	clock-frequency = <600000000>;
	clocks = <&ccu CLK_PLL_PERI0_2X>, <&dsp_ccu CLK_DSP_DSP>,
		 <&ccu CLK_DSP>, <&dsp_ccu CLK_DSP_CFG>, <&r_ccu CLK_R_AHB>;
	clock-names = "pll", "dsp-mod", "mod", "cfg", "ahbs";
	resets = <&dsp_ccu RST_BUS_DSP_CORE>, <&dsp_ccu RST_BUS_DSP_CFG>,
		 <&dsp_ccu RST_BUS_DSP_DBG>;
	reset-names = "mod-rst", "cfg-rst", "dbg-rst";
	reg = <0x0 0x07140364 0x0 0x04>,  /* REMAP_CTRL_REG (sram-for-cpux) */
	      <0x0 0x07300000 0x0 0x100>; /* hifi4-cfg */
	reg-names = "sram-for-cpux", "hifi4-cfg";
	mboxes = <&msgbox 4>;
	mbox-names = "arm-kick";
	memory-region = <&dsp0ddr_reserved>, <&dsp_vdev0buffer>,
			<&dsp_vdev0vring0>, <&dsp_vdev0vring1>,
			<&dsp0iram_reserved>, <&dsp0dram0_reserved>, <&dsp0dram1_reserved>;
	status = "okay";
};

rproc: remoteproc@7130000 {
	compatible = "allwinner,sun55i-a523-rproc",
	             "allwinner,sun55i-a527-rproc";
	reg = <0x07130000 0x1000>,      /* "cfg": E907 CFG & boot-address registers */
	      <0x07280000 0x40000>,     /* "r_sram": SRAM_A3 Space 0 (256 KB on A523, 512 KB on T527) */
	      <0x072c0000 0x40000>,     /* "r_sram1": SRAM_A3 Space 1 (256 KB on A523, 512 KB on T527) */
	      <0x07010364 0x4>;         /* "remap": REMAP_CTRL_REG (0x07010364 on A523, 0x07140364 on T527) */
	reg-names = "cfg", "r_sram", "r_sram1", "remap";
	clocks = <&mcu_ccu CLK_BUS_MCU_RISCV_CFG>,
	         <&mcu_ccu CLK_MCU_RISCV>;
	clock-names = "bus", "core";
	resets = <&mcu_ccu RST_BUS_MCU_RISCV_CFG>,
	         <&mcu_ccu RST_BUS_MCU_RISCV_CORE>;
	reset-names = "cfg", "core";
	mboxes = <&msgbox 0>, <&msgbox 1>;
	mbox-names = "rx", "tx";
	status = "disabled";
};
```
```

---

## 8. Summary & Key Takeaways

1. **`MCU_SYS` is the unified heterogeneous domain** containing both the XuanTie RISC-V and Tensilica HiFi4 DSP cores.
2. **`REMAP_CTRL_REG` (Offset `0x364`) is the key hardware switch**:
   - Bit 0 (`MCU_RAM_REMAP`): Controls whether DSP local RAM (`0x00400000`–`0x0044FFFF`) is shared with system CPUX.
   - Bit 1 (`SRAMA3_2_RAM_REMAP`): Controls whether `SRAMA3_2` (`0x07280000` / `0x072c0000`, 512 KB / 256 KB) is bridged into `MCU_SYS`.
3. **`SRAMA3_2` can be utilized for**:
   - High-throughput zero-wait-state RISC-V execution at DA `0x40000000`.
   - Dedicated HiFi4 DSP workspace.
   - Zero-jitter, lockless SPSC ring buffers and direct shared memory IPC between RISC-V, DSP, and Linux.
4. **Linux RemoteProc must actively manage `REMAP_CTRL_REG`**:
   - Un-gating Bit 1 during `prepare()` ensures that firmware segments placed at DA `0x40000000` fetch and execute without bus faults.
