# ⚡ Common Subsystem Guide: XuanTie RISC-V Co-Processor & Linux RemoteProc

This document details the **XuanTie E907 / E906 RISC-V Co-Processor Subsystem**, memory routing, lifecycle management via the mainline Linux `remoteproc` framework ([`sunxi_rproc.c`](../../project-cubie-a5e/patches/linux/0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch)), and inter-processor communication.

---

## 1. Co-Processor Hardware Architecture

The XuanTie E907 (RV32IMAFCP) co-processor provides hard real-time deterministic execution alongside the high-performance ARM Cortex cluster:

| Memory Region | Physical ARM Host Address | Local RISC-V Core Address | Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **ITCM** (Instruction TCM) | `0x07110000` | `0x00000000` | 64 KiB | Single-cycle zero-wait-state code execution |
| **DTCM** (Data TCM) | `0x07120000` | `0x00080000` | 64 KiB | Single-cycle zero-wait-state stack & data |
| **SRAM C** (Shared Memory) | `0x07130000` | `0x07130000` | 128 KiB | Lock-free SPSC circular ring buffers & IPC |
| **CCU Registers** | `0x07010000` | — | — | Clock gate (`0x0020`) & Core Reset (`0x0100`) |

---

## 2. Kernel Driver Architecture (`sunxi_rproc.c`)

The Linux kernel driver (`drivers/remoteproc/sunxi_rproc.c`) standardizes lifecycle control:

1. **Automatic Section Routing (`da_to_va`)**:
   - Parses ELF Program Headers (`paddr` / `da`) and copies `.text` to ITCM (`0x07110000`), `.data`/`.bss` to DTCM (`0x07120000`), and static descriptors to SRAM C (`0x07130000`).
2. **Clock Tree & Reset Sequencing**:
   - On `start`: Asserts core reset $\rightarrow$ Enables CCU clock gate (`0x07010020` bit 0) $\rightarrow$ De-asserts core reset (`0x07010100` bit 0).
   - On `stop`: Asserts core reset $\rightarrow$ Disables CCU clock gate.
3. **Debugfs Firmware Trace Streaming**:
   - The `.resource_table` embedded in the ELF dynamically instantiates `/sys/kernel/debug/remoteproc/remoteproc0/trace0`, streaming firmware logs directly to user-space.

---

## 3. Sysfs Operational Lifecycle

### 1. Stage Firmware ELF
```bash
cp my_flight_firmware.elf /lib/firmware/riscv-firmware.elf
```

### 2. Select & Boot Remote Processor
```bash
# Assign firmware filename
echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware

# Boot the XuanTie E907 core
echo start > /sys/class/remoteproc/remoteproc0/state

# Verify active status
cat /sys/class/remoteproc/remoteproc0/state
# Output: running
```

### 3. Stream Live Debug Logs
```bash
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

### 4. Stop Core
```bash
echo stop > /sys/class/remoteproc/remoteproc0/state
```

---

## 4. Hardware Verification via OpenOCD & JTAG

For low-level hardware verification independent of Linux software, OpenOCD interfaces with the Core Debug Module (DM):

```bash
openocd -f board/radxa/cubie_a5e/openocd_t527_local.cfg
```
- In Telnet (`localhost:4444`):
  ```text
  > riscv.cpu curstate
  running (or halted)
  > riscv.cpu mdw 0x07110000 8
  ```
