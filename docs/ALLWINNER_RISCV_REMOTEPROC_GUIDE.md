# Allwinner XuanTie RISC-V Remote Processor (`sunxi_rproc`) Dual-SoC Architecture Guide

**Author:** tcmichals (`tcmichals@gmail.com`)  
**Date:** September 1, 2026  
**License:** GPL-2.0-only  
**Target Platforms:** Radxa Cubie A5E (Allwinner A523/A527) & Radxa Cubie A7A (Allwinner A733)

---

## 1. Executive Summary & SoC Comparison

* **Allwinner T527 / A523 / A527 (sun55i / Cubie A5E)**: Integrates a dedicated **T-Head XuanTie E906 / E907 32-bit RISC-V core** (RV32IMAFDC + FPU) inside an open MCU peripheral domain (`0x07100000+`). Full, unrestricted Linux `remoteproc` support with ITCM, DTCM, MCU SRAM, and mailbox IPC.
* **Allwinner A733 (sun60iw2 / Cubie A7A & A7Z)**: Integrates a **XuanTie E902** (RV32EMC) inside the CPUS / Always-On power management domain (`0x07000000+`). The E902 is dedicated strictly to power management running vendor `scp.fex` loaded by U-Boot / `boot0` for PMIC power rail control. Linux `remoteproc` is deactivated for A733.

---

## 2. Architectural Comparison Matrix

| Architectural Feature | Radxa Cubie A5E (T527 / A523 / sun55i) | Radxa Cubie A7A / A7Z (A733 / sun60iw2) |
| :--- | :--- | :--- |
| **Coprocessor IP** | **XuanTie E906 / E907** (RV32IMAFDC + FPU) | XuanTie E902 (RV32EMC) |
| **Subsystem Domain** | **Dedicated MCU Domain** (`0x07100000+`) | CPUS / Always-On Domain (`0x07000000+`) |
| **Primary SRAM / TCM** | **64 KB ITCM, 64 KB DTCM, 256 KB MCU SRAM** | System SRAM A2 (`0x00040000`), DRAM |
| **Clock / Reset Control**| **Dedicated `mcu_ccu` (`0x07102000`)** | Shared CPUS `r_ccu` (`0x07010000`) |
| **Bootloader Role** | **None** (Untouched by U-Boot) | U-Boot loads `scp.fex` for PMIC power |
| **Linux Remoteproc** | **Full Native Support (`sunxi_rproc.c`)** | **Deactivated (Dedicated to `scp.fex`)** |

---

## 3. Detailed SoC Implementations

### A. Radxa Cubie A5E (Allwinner T527 / A523)

On the T527/A523 SoC, the Linux kernel has direct Non-Secure access to the MCU CCU, TCMs, and SRAM apertures. Linux remoteproc maps ITCM, DTCM, and SRAM directly into its kernel address space:

```dts
/* A5E / T527 Device Tree Nodes */
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

* **Linker Layout (`firmware.ld`)**:
  * `ITCM (rx)`: `ORIGIN = 0x00000000, LENGTH = 64K`
  * `DTCM (rwx)`: `ORIGIN = 0x00080000, LENGTH = 64K`
  * `SRAM (rwx)`: `ORIGIN = 0x07280000, LENGTH = 256K`

---

### B. Radxa Cubie A7A & A7Z (Allwinner A733) — Dedicated Power Management Core

On the A733 SoC:
* **The E902 is for Power Management**: It is initialized during the multi-stage boot sequence by `boot0` and U-Boot with `scp.fex`.
* **PMIC Control**: `scp.fex` connects to the AXP8191 PMIC over RSB (`r_rsb: rsb@7083000`) and activates `DCDC1` (supplying power to the FE1.1S USB hub and AIC8800 Wi-Fi 6 module) and system power rails.
* **DRAM Protection**: `sun60i-a733-cubie-a7a.dts` reserves DRAM at `0x40014000` (`scp_dram: scp@40014000`) with `no-map` so Linux never collides with SCP memory.
* **Remoteproc**: Decommissioned on A733.

#### Boot & Execution Flow:
1. **Linux Remoteproc (`sunxi_rproc.c`)**:
   * Reads ELF headers and maps `.vectors`, `.text`, and `.data` into Dedicated RISC-V SRAM (`0x07280000` / Core `0x00000000`).
   * Configures MCU CCU bus bridges (`TZMA0`, `TZMA1`, `PUBSRAM`, `MBUS`).
   * Releases core reset by writing `0x00070001` to `0x07102124`.
2. **XuanTie E907 Core**:
   * Begins execution directly at hardware reset vector `0x00000000` (Dedicated SRAM).
   * Runs `startup.S` and jumps to `_start`.
   * Telemetry and shared buffers are accessed in Shared System SRAM A2 (`0x00040000`) or DDR Carveout (`0x4E000000`).

---

## 4. Resource Table & Debugfs Trace Logging

To expose real-time logging to Linux userspace without serial UART cables, the firmware exports a standard `struct fw_rsc_trace` in `.resource_table`:

```c
#define TRACE_BUF_DA    0x4E010000  /* 64 KB offset in DDR carveout */
#define TRACE_BUF_LEN   4096        /* 4 KB circular ASCII buffer */

struct cubie_resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1];
    struct fw_rsc_trace trace;
} __attribute__((packed));

__attribute__((used, section(".resource_table"), aligned(4)))
const struct cubie_resource_table resource_table = {
    .ver = 1,
    .num = 1,
    .reserved = {0, 0},
    .offset = { offsetof(struct cubie_resource_table, trace) },
    .trace = {
        .type     = RSC_TRACE,
        .da       = TRACE_BUF_DA,
        .len      = TRACE_BUF_LEN,
        .reserved = 0,
        .name     = "trace0",
    },
};
```

---

## 5. Build, Deployment, & Verification Procedures (Cubie A5E)

### A. Building the Firmware
```bash
# Clean and compile the RISC-V application
make -C bld.a5e riscv-firmware-dirclean riscv-firmware
```

### B. Live Updating Over the Network
With network active (`192.168.1.33`):
```bash
# Push firmware via deployment script or SCP:
./project-cubie-a5e/scripts/push-riscv-firmware.sh 192.168.1.33
```

### C. Controlling Core & Reading Trace on Target
```bash
# 1. Restart co-processor:
echo stop > /sys/class/remoteproc/remoteproc0/state
echo start > /sys/class/remoteproc/remoteproc0/state

# 2. View live real-time heartbeat:
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

