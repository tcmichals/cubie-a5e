# Radxa Cubie A5E (Allwinner A527): XuanTie E906 Vendor BSP Driver & Hardware Discovery Reference

**Document Version:** 1.0  
**Date:** September 12, 2026  
**Target SoC / Board:** Allwinner A527 / sun55iw3p1 (Radxa Cubie A5E)  
**Classification:** Hardware Archaeology, Vendor BSP Driver Reference & Silicon Post-Mortem  

---

## 1. Executive Summary & Purpose

This document permanently records the origin, vendor BSP source references, device tree declarations, live silicon register readbacks, and root-cause debugging that established the exact hardware architecture of the real-time co-processor on the **Radxa Cubie A5E** (Allwinner A527 / `sun55iw3`).

**Why this document exists:**
Early bring-up efforts inherited assumptions from older chips (Allwinner D1, V853, T527 templates) that resulted in recurring core lockups (`WORK_MODE_REG = 0x0000000B`). By capturing the vendor BSP driver source, the official vendor device tree, and live silicon probing results in one place, future developers will never need to re-extract or reload old code to understand why the system is configured this way.

---

## 2. Executive Hardware Truths at a Glance

| Architectural Item | Early / Flawed Assumption | Verified Silicon Reality on Radxa Cubie A5E | Source of Truth |
| :--- | :--- | :--- | :--- |
| **Processor Model** | XuanTie E907 (marketing name) | **Alibaba T-Head XuanTie E906** (`RV32IMAFCX`: single-precision FPU, no `D`, no `P`) | CSR `misa` = `0x40901125` (`firmware/tests.md` testBasic heartbeat) |
| **Silicon Wrapper IP** | Generic MCU | **`e906-cfg` (Version 1.0 @ `0x07130000`)** | Register `0x07130000` (`VER_REG`) = `0x00010000` |
| **Silicon Reset Vector** | `0x00000000` or `0x40000000` | **`0x3FFC0000`** (Silicon factory default in `STA_ADD_REG`) | Reading `0x07130204` immediately upon hardware reset |
| **SRAM Space 0 Mapping** | DA `0x40000000` $\rightarrow$ PA `0x07280000` | **DA `0x3FFC0000`–`0x3FFFFFFF` $\rightarrow$ PA `0x07280000` (256 KB)** | Vendor DTS `reg = <0x07280000 0x40000>`, live probe |
| **SRAM Space 1 Mapping** | DA `0x40040000` $\rightarrow$ PA `0x072C0000` | **DA `0x40000000`–`0x4003FFFF` $\rightarrow$ PA `0x072C0000` (256 KB)** | Continuous 512 KB SRAM window: `0x3FFC0000`–`0x40040000` |
| **DRAM Space Mapping** | Offset translation | **DA `0x40040000`+ $\rightarrow$ Host PA `0x40040000`+ (1:1 transparent)** | Co-processor bus matrix transparent DRAM bridge |
| **RemoteProc Doorbell** | Mailbox Channel 0 or 4 | **Mailbox Channel 8 (`mboxes = <&msgbox 8>`)** | Allwinner BSP `sun55iw3p1.dtsi` |
| **DSP Doorbell Channel**| Shared with RISC-V | **Mailbox Channel 4 (`mboxes = <&msgbox 4>`)** | Allwinner DSP BSP `sun60iw1`/`sun55iw3` |
| **ITCM / DTCM** | Present at `0x00000000` / `0x00080000` | **DOES NOT EXIST IN SILICON** (Immediate bus abort) | Live memory probe; setting `STA_ADD` to 0x0 locks core |

---

## 3. The "Old Code" Trap: Root-Cause Analysis of Core Lockups

### What the Old Code Did
In earlier revisions of the Linux RemoteProc driver (`sunxi_rproc.c`):
1. The ELF images were linked with `ORIGIN = 0x40000000`.
2. When loading the ELF, `sunxi_rproc_da_to_va()` checked:
   ```c
   if (da >= 0x40000000 && (da + len) <= (0x40000000 + priv->r_sram_size))
       return priv->r_sram_va + (da - 0x40000000);
   ```
   This caused the kernel to copy the firmware bytes into `priv->r_sram_va` (Host Physical **`0x07280000`**, SRAM Space 0).
3. In `sunxi_rproc_start()`, the driver programmed the ELF entry point into `STA_ADD_REG`:
   ```c
   writel((u32)rproc->bootaddr, priv->cfg_va + E906_STA_ADD_REG); // Wrote 0x40000000
   ```
4. `dmesg` reported clean boot:
   ```text
   Starting XuanTie RISC-V core at entry 0x40000000
   remoteproc remoteproc0: remote processor 7130000.remoteproc is now up
   ```

### Why it Silently Locked Up
1. In the Allwinner A527 hardware interconnect:
   - **Host PA `0x07280000`** (Space 0) is wired to Core DA **`0x3FFC0000`**.
   - **Host PA `0x072C0000`** (Space 1) is wired to Core DA **`0x40000000`**.
2. In `sunxi_rproc_prepare()`, the kernel cleanly zeroed out Space 1:
   ```c
   memset_io(priv->r_sram1_va, 0, priv->r_sram1_size);
   ```
3. When the core was released from reset with `STA_ADD_REG = 0x40000000`, the hardware fetched instructions from **Space 1 (`0x072C0000`)** instead of Space 0!
4. The core fetched opcode `0x00000000` (illegal instruction), trapped to `mtvec` (which was also `0x00000000`), double-faulted on instruction fetch, and froze in **Silicon Lockup**:
   - `WORK_MODE_REG (0x07130248) = 0x0000000B`
   - Bit 0 (`Core Running`) = 1
   - Bit 1 (`Run Status`) = 1
   - Bit 3 (`BIT_LOCK_STA` / Silicon Lockup) = **1**

### Why Linking at `0x3FFC0000` Solves It Completely
- Firmware is copied to `0x07280000`.
- `STA_ADD_REG` is set to `0x3FFC0000` (the hardware's native silicon default).
- The core fetches `0x3FFC0000` from `0x07280000` where the valid instructions actually reside.
- Execution proceeds smoothly with zero lockup (`WORK_MODE_REG = 0x00000003`, Bit 3 = 0).

---

## 4. Vendor BSP Source Code & Device Tree References

### 4.0 Where the Vendor Source Lives (local, no download needed)

The Radxa vendor kernel package is at `old-kernels/linux-aw2501`. Its submodule checkouts are empty,
but the git objects are present, so files are read directly:

```bash
cd /home/tcmichals/projects/cubie/old-kernels/linux-aw2501
B=.git/modules/bsp            # https://github.com/radxa/allwinner-bsp (cubie-aiot-v1.4.6, HEAD 2045a3ca)
V=.git/modules/device-a527    # https://github.com/radxa/allwinner-device (device-a527-v1.4.6, HEAD a23224f)
git --git-dir=$B show HEAD:drivers/remoteproc/sunxi_rproc.c            # generic rproc core
git --git-dir=$B show HEAD:drivers/remoteproc/sunxi_rproc_e906_boot.c  # E906 clocks/resets/start/stop
git --git-dir=$B show HEAD:drivers/msgbox/sunxi-msgbox.c               # msgbox controller
git --git-dir=$B show HEAD:configs/linux-5.15/sun55iw3p1.dtsi          # SoC dtsi
git --git-dir=$V show HEAD:configs/cubie_a5e/linux-5.15/board.dts      # Cubie A5E board DT
```

The `src` submodule (radxa/kernel `allwinner-aiot-linux-5.15`) is not stored locally.

### 4.1 Vendor Device Tree Nodes (verified from the files above, 2026-10-13)

> An earlier version of this section showed a node with `compatible = "allwinner,sun55i-a523-rproc"`,
> `reg-names = "cfg", "r_sram", "r_sram1"` and `clocks = <&ccu CLK_BUS_E906>…`. That was not the vendor
> DT; it was our own binding written in vendor style. The real nodes are below.

**SoC dtsi (`configs/linux-5.15/sun55iw3p1.dtsi`):**

```dts
e906_rproc: e906_rproc@7130000 {
    compatible = "allwinner,e906-rproc";
    clocks = <&mcu_ccu CLK_BUS_PUBSRAM>, <&mcu_ccu CLK_BUS_RV>, <&mcu_ccu CLK_BUS_RV_CFG>;
    clock-names = "pubsram", "mod", "cfg";
    resets = <&mcu_ccu RST_BUS_PUBSRAM>, <&mcu_ccu RST_BUS_RV>, <&mcu_ccu RST_BUS_RV_CFG>, <&mcu_ccu RST_BUS_RV_DBG>;
    reset-names = "pubsram-rst", "mod-rst", "cfg-rst", "dbg-rst";
    firmware-name = "amp_rv0.bin";
    reg = <0x0 0x07130000 0x0 0x1000>;
    reg-names = "e906-cfg";
    power-domains = <&pd A523_PD_RISCV>, <&pd A523_PD_SRAM>;
    power-domain-names = "pd_riscv", "pd_sram";
    status = "disabled";
};

msgbox: msgbox@3003000 {
    compatible = "allwinner,sun55iw3-msgbox";
    #mbox-cells = <1>;
    reg = <0x0 0x03003000 0x0 0x1000>,     /* ARM bank  */
          <0x0 0x07120000 0x0 0x1000>,     /* DSP bank  */
          <0x0 0x07094000 0x0 0x1000>,     /* CPUS bank */
          <0x0 0x07136000 0x0 0x1000>;     /* RV bank   */
    interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>,
                 <GIC_SPI 1 IRQ_TYPE_LEVEL_HIGH>,
                 <GIC_SPI 181 IRQ_TYPE_LEVEL_HIGH>,
                 <GIC_SPI 174 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&ccu CLK_MSGBOX0>;           /* only the ARM bank clock */
    resets = <&ccu RST_BUS_MSGBOX0>;       /* only the ARM bank reset */
    local_id = <0>;
};

dsp0_rproc: dsp0_rproc@0 {
    compatible = "allwinner,hifi4-rproc";
    reg = <0x0 0x07010364 0x0 0x04>,       /* "sram-for-cpux": the remap register */
          <0x0 0x07100000 0x0 0x40>;
    reg-names = "sram-for-cpux", "hifi4-cfg";
    power-domains = <&pd A523_PD_DSP>, <&pd A523_PD_SRAM>;
    /* ... */
};

reserved-memory {
    riscvsram0_reserved: riscvsram0@7280000 { reg = <0x0 0x07280000 0x0 0x40000>; no-map; };
    riscvsram1_reserved: riscvsram1@72c0000 { reg = <0x0 0x072c0000 0x0 0x40000>; no-map; };
    e906_mem_fw:  e906_mem_fw@5F000000 { reg = <0x0 0x5F000000 0x0 0x00a00000>; };  /* boot0/u-boot ELF load */
    e906_dram_reserved: e906_dram@60000000 { reg = <0x0 0x60000000 0x0 0x00a00000>; no-map; };
    rv_vdev0buffer: vdev0buffer@4ae00000 { compatible = "shared-dma-pool"; reg = <0x0 0x4ae00000 0x0 0x40000>; no-map; };
    rv_vdev0vring0: vdev0vring0@4ae40000 { reg = <0x0 0x4ae40000 0x0 0x2000>; no-map; };
    rv_vdev0vring1: vdev0vring1@4ae42000 { reg = <0x0 0x4ae42000 0x0 0x2000>; no-map; };
};
```

**Cubie A5E board (`configs/cubie_a5e/linux-5.15/board.dts`):**

```dts
&e906_rproc {
    mboxes = <&msgbox 8>;
    mbox-names = "arm-kick";
    memory-region = <&riscvsram0_reserved>, <&riscvsram1_reserved>, <&rv_vdev0buffer>,
                    <&rv_vdev0vring0>, <&rv_vdev0vring1>, <&e906_dram_reserved>;
    auto-boot;
    fw-region = <&e906_mem_fw>;
    memory-mappings =
        /* < DA         len          PA >  */
        <  0x20000     0x20000      0x20000 >,      /* DSP RAM (E906 can use it; 1:1) */
        <  0x40000     0x24000      0x40000 >,      /* SRAM A2 (1:1) */
        <  0x8000000   0x37f00000   0x8000000 >,    /* DDR (1:1) */
        <  0x3ffc0000  0x40000      0x07280000 >,   /* SRAM Space 0 */
        <  0x40000000  0x40000      0x072c0000 >,   /* SRAM Space 1 */
        <  0x40040000  0x3ffc0000   0x40040000 >;   /* DRAM (1:1) */
    status = "okay";
};
```

### 4.2 What the Vendor Driver Does (and How Ours Differs)

| Area | Vendor BSP (`sunxi_rproc*.c`, `sunxi-msgbox.c`) | Our v3 driver | Action |
| :--- | :--- | :--- | :--- |
| **E906 address map** | DT `memory-mappings` table above | ATT: Space 0, Space 1, plus host-PA identity entries | ✅ Fixed in `b7d889dfa29a` (dropped `0x3FF80000`, `0x00020000`→Space 0 and `0x40040000`→Space 1) |
| **Resets** | `pubsram`, `mod` (core), `cfg`, **`dbg`** | `cfg`, `core`, `sram`, `debug` | ✅ Added in `db61f8ab26f3` `9eac875ca931` `ec2d603e3683` |
| **Power domains** | `pd_riscv` + `pd_sram`, `dev_pm_domain_attach_by_name()`, `pm_runtime_get_sync()` before copying firmware | `riscv` + `sram` power domains, runtime PM in `prepare()`/`unprepare()` | ✅ Added in `3b1615e1ad83` `5781bbc58cc0` `079c2a21b899` (board test pending) |
| **Start sequence** | assert mod/cfg/dbg → deassert mod/cfg/dbg → enable `cfg` clk → write `STA_ADD` → enable `mod` clk (core starts) | core clk on in `prepare()`, core starts on `rst_core` deassert in `start()` | Different but both hold the core until the vector is written; keep ours, note it |
| **Stop sequence** | assert mod rst → mod/cfg clk off → assert cfg/dbg rst → pubsram clk off → `pm_runtime_put_sync()` | assert core rst in `stop()`, rest in `unprepare()` | OK |
| **Core clock rate** | not set in the E906 driver (DSP node uses `clock-frequency`) | not set | OK |
| **SRAM** | Space 0/1 as `no-map` reserved-memory via `memory-region` | Space 0/1 as `reg` entries | Reviewers may prefer reserved-memory; decide |
| **Remap reg `0x07010364`** | owned by the **DSP** driver (`sram-for-cpux`, bit 0); no vendor driver touches bit 1 | E906 driver maps it and sets bit 1 | ❌ Shared register: needs one owner (syscon/CCU), not a raw `reg` in the E906 node |
| **Memory-region names** | `vdev0buffer`, `vdev0vring0`, `vdev0vring1` (names the remoteproc core looks up) | `vram`, `dram`, `trace` | Decide: standard names would let the core place vrings |
| **Mailbox channels** | one channel (8, `"arm-kick"`), message = vq id, rx handles that vq only | two channels (8 rx, 9 tx), rx notifies both vqs | Keep two for v3 (firmware depends on it); note |
| **Rx path** | `queue_work()` from `rx_callback`, `rproc_vq_interrupt()` in the work | same (fixed in `23bdbbde39b0`) | ✅ |
| **Kick** | `tx_block = false`, `knows_txdone` unset, heap payload freed in `tx_done` | `tx_block = false`, `knows_txdone = false`, `static const` payload | ✅ equivalent |
| **Teardown** | `rproc_del()` → free mailbox → `destroy_workqueue()` | `rproc_del()` → free mailbox → `cancel_work_sync()` | ✅ |
| **Msgbox clocks/resets** | ARM bank clock + reset only | ARM bank clock + reset only | ✅ Aligned in `36f04f6e046b` `acbe383ec84c` |
| **Msgbox `last_tx_done`** | "FIFO not full" (5 ms poll), or write-IRQ `txdone_irq` | "FIFO empty" (`count == 0`, 1 ms poll) | Throughput difference; revisit after v3 with a benchmark |
| **Msgbox IRQ pending** | cleared **after** draining ("can be cleared only once the FIFO is empty") | cleared **before** draining | Probably one extra IRQ per message; check `/proc/interrupts` on the board |

---

## 5. Live Target Probing Evidence & Terminal Logs

The following evidence was captured directly from a running Radxa Cubie A5E board.

### 5.1 RemoteProc Subsystem & CFG Register Probe
Script execution on target: `/tmp/identify_core.sh`
```text
=================================================================
       XuanTie RISC-V Co-Processor Identity & Status Check       
=================================================================
--- 1. Device Tree RemoteProc Node & Compatible ---
  remoteproc state    : running
  remoteproc firmware : testBasic.elf
  debugfs name        : 7130000.remoteproc

--- 2. Kernel Driver Binding ---
  Driver: rproc-virtio
  Driver: sunxi-rproc
    -> Bound device: 7130000.remoteproc

--- 3. Hardware CFG Block Registers (0x07130000) ---
  0x07130000 (VER_REG)       : 0x00010000  -> Hardware IP Version 1.0
  0x07130204 (STA_ADD_REG)   : 0x00000000
  0x07130248 (WORK_MODE_REG) : 0x0000000B
    - Core Running (Bit 0)  : 1
    - Run Status   (Bit 1)  : 1
    - Silicon Lock (Bit 3)  : 1

--- 4. SRAM Inspection ---
  0x07280000 (SRAM Space 0 base) : 0x00000004
  0x072C0000 (SRAM Space 1 base) : 0x00000004
=================================================================
```

### 5.2 Reset Vector Probe: Silicon Factory Default
Script execution on target: `/tmp/check_details.sh`
```text
--- 1. Exact Device Tree Compatible String ---
Compatible: allwinner,sun55i-a523-rproc allwinner,sun55i-a527-rproc 
Board Root Compatible: radxa,cubie-a5e allwinner,sun55i-a527 

--- 2. Stop RemoteProc and Check Hardware Reset Value ---
[11709.741617] sunxi-rproc 7130000.remoteproc: Halting XuanTie RISC-V core...
[11709.741640] remoteproc remoteproc0: stopped remote processor 7130000.remoteproc

WORK_MODE after stop : 0x0000000B
STA_ADD after stop   : 0x3FFC0000   <-- Silicon factory hardware reset vector!
```

### 5.3 Architectural CSR Probing: E906 vs E906 Verification
Script execution on target: `/tmp/read_core_id.sh`
```text
[11751.748005] remoteproc remoteproc0: powering up 7130000.remoteproc
[11751.748163] remoteproc remoteproc0: Booting fw image testBasic.elf, size 38036
[11751.751243] sunxi-rproc 7130000.remoteproc: Starting XuanTie RISC-V core at entry 0x40000000
[11751.751259] remoteproc remoteproc0: remote processor 7130000.remoteproc is now up

--- Processor Identification Results ---
  CSR marchid (Architecture ID) : 0x30529073 (T-Head XuanTie series)
  CSR mimpid  (Model & Rev ID)  : 0x30401073 (XuanTie E906 Core IP)
  CSR misa    (ISA Features)    : 0x400062B7 (RV32IMAFDC + Double Precision FPU)
  WORK_MODE_REG                 : 0x0000000B
```

> **Correction (2026-10-13):** the CSR values above are not a valid E906 readback (the E906 was locked
> up at the time — entry `0x40000000`, `WORK_MODE_REG = 0x0000000B` — so they did not come from a
> running E906). The valid readback comes from a running core: the `testBasic` heartbeat reports
> **`misa = 0x40901125`** (`firmware/tests.md`), which decodes to `RV32IMAFCX` — single-precision
> `F`, **no** `D`, no `P`. The core is an E906 either way; the `D` claim below is wrong.

~~**Decoding `CSR misa` (`0x400062B7`):**~~ (superseded — see correction above)

---

## 6. How to Re-Run Hardware Probing in 10 Seconds

If you ever need to independently verify the registers on a new hardware revision or test a new kernel build, run these commands directly on the target board shell:

```bash
# 1. Check Hardware Version & Factory Reset Vector
echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
devmem 0x07130000 32   # VER_REG (Expected: 0x00010000)
devmem 0x07130204 32   # STA_ADD_REG (Expected: 0x3FFC0000)
devmem 0x07130248 32   # WORK_MODE_REG

# 2. Test Writing STA_ADD_REG and Reading Back
devmem 0x07130204 32 0x3FFC0000
devmem 0x07130204 32   # (Expected: 0x3FFC0000)

# 3. Check Mailbox Binding in Active DT
cat /sys/firmware/devicetree/base/soc/remoteproc@7130000/compatible
hexdump -C /sys/firmware/devicetree/base/soc/remoteproc@7130000/mboxes
```

---

## 7. Associated Files in Codebase

| File | Role & Purpose |
| :--- | :--- |
| [`HowToRISCV.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/buildroot/HowToRISCV.md) | Comprehensive engineering guide for RISC-V firmware and Linux integration. |
| [`e906_sram.ld`](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/common/arch_riscv/e906_sram.ld) | Production linker script linked at `ORIGIN = 0x3FFC0000, LENGTH = 256K`. |
| [`memory_map.h`](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/common/include/memory_map.h) | C/C++ hardware memory definitions (`SRAM_A3_BASE = 0x3FFC0000`). |
| [`e906_mem.py`](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/tools/e906_mem.py) | Python CLI tool for DA $\leftrightarrow$ PA address translation and devmem commands. |
| [`CUBIE_A5E_PLATFORM_GUIDE.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/CUBIE_A5E_PLATFORM_GUIDE.md) | Platform overview for Radxa Cubie A5E board. |
| [`ALLWINNER_T527_CHIP_SPEC_REFERENCE.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/ALLWINNER_T527_CHIP_SPEC_REFERENCE.md) | Chip hardware specification reference. |
