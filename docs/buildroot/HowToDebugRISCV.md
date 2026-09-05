# How To Debug XuanTie RISC-V Co-Processors (Allwinner T527 / Radxa Cubie A5E)

> [!NOTE]
> **Co-Processor Architecture Note**:
> - **Allwinner T527 (Radxa Cubie A5E)**: Features an independent XuanTie E906/E907 RISC-V co-processor dedicated to user/flight-stack real-time tasks, supported natively by Linux `remoteproc`.
> - **Allwinner A733 (Radxa Cubie A7A / A7Z)**: Features an embedded XuanTie E902 core dedicated strictly to CPUS / Always-On power management (`scp.fex`) loaded at boot time by U-Boot / `boot0`. Linux remoteproc is not used on A733.

---

## 1. Direct Memory Debug (`dmem`) Architecture & Silicon Comparison

### How `dmem` Works on Other SoCs (TI, STMicroelectronics, NXP)
In modern heterogeneous SoCs from Texas Instruments (AM62x / AM64x / K3), STMicroelectronics (STM32MP1 / STM32MP2), and NXP (i.MX), the hardware debug module (ARM CoreSight or RISC-V Debug Module) is routed directly onto the main system bus as a memory-mapped `dmem` interface.

This architecture enables **self-hosted, JTAG-less on-chip debugging**:
- OpenOCD running natively on the Linux host accesses debug registers directly using the OpenOCD `dmem` driver (`adapter driver dmem`).
- Developers can run interactive GDB sessions with hardware breakpoints and register inspection directly over SSH, without requiring an external physical USB-JTAG debug probe.

### Current Allwinner T527 Silicon Reality
Current **Allwinner T527 silicon does not implement a memory-mapped `dmem` bus interface** for the XuanTie RISC-V Debug Module (DM) into the non-secure ARM interconnect.

Because the debug module registers are not exposed to the ARM system bus, target-side OpenOCD over a memory-mapped bus is not supported on this revision. We hope that Allwinner will support a memory-mapped `dmem` interface in future SoC revisions so that developers can take full advantage of native Linux-hosted OpenOCD and GDB remote debugging.

---

## 2. Recommended Debugging Facilities on T527

For co-processor firmware development on the T527, developers have several clean, high-performance mechanisms:

```
 ┌─────────────────────────────────────────────────────────────┐
 │                ARM64 Mainline Linux Host                    │
 │                                                             │
 │  ┌───────────────────────┐       ┌───────────────────────┐  │
 │  │ RemoteProc Subsystem  │       │  rpmsg_char / IPC     │  │
 │  │ /sys/.../remoteproc0  │       │  /dev/rpmsg0          │  │
 │  └──────────┬────────────┘       └───────────┬───────────┘  │
 └─────────────┼────────────────────────────────┼──────────────┘
               │                                │
               ▼                                ▼
 ┌─────────────────────────────────────────────────────────────┐
 │              Shared Memory & Hardware Interconnect          │
 │                                                             │
 │  • RemoteProc Trace0 Buffer (DDR Carveout @ 0x48000000, 4KB) │
 │  • Dedicated MCU SRAM Ring Buffers (0x07280000/0x3FFC0000)  │
 │  • Hardware Mailbox Doorbell IRQs (0x03003000)              │
 └─────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │           XuanTie E907 Real-Time Co-Processor (200 MHz)     │
 │                                                             │
 │  • Dedicated S_UART0 Serial Console (0x07080000, 115.2k)    │
 │  • 128 KB PubSRAM C (0x00020000) + 256 KB Dedicated R_SRAM  │
 │  • External JTAG Test Interface (Physical Probe)            │
 └─────────────────────────────────────────────────────────────┘
```

1. **RemoteProc Trace Buffer (`trace0`)**: Real-time circular log buffer mapped into `/sys/kernel/debug/remoteproc/remoteproc0/trace0` (phys `0x48000000`).
2. **Dedicated Hardware UART (`S_UART0`)**: Low-latency, non-blocking serial console at `0x07080000` (115200 baud).
3. **Lock-Free Shared SRAM Ring Buffers**: High-throughput shared memory telemetry in Dedicated MCU SRAM (`0x3FFC0000` Core / `0x07280000` Host) or PubSRAM C (`0x00020000`).
4. **Hardware Mailbox Doorbell IRQ & RPMsg**: Sub-microsecond inter-processor communication.
5. **Physical Hardware JTAG Probe**: Standard JTAG header connection with external debug probes (CK-Link, J-Link, FTDI) for interactive hardware halting/stepping.

---

## 3. RemoteProc Firmware Loading & Trace Logging

### Step 1: Deploy the Firmware ELF to the Target

```bash
# On your development host:
scp cubie-a5e/riscv-firmware/bin/testStringBinaryTrace0.elf root@cubie-a5e:/lib/firmware/testStringBinaryTrace0.elf
```

### Step 2: Boot the Co-Processor from Linux

```bash
# On the target (ARM Linux shell):

# Point remoteproc to the firmware binary
echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware

# Start execution
echo start > /sys/class/remoteproc/remoteproc0/state

# Verify running state
cat /sys/class/remoteproc/remoteproc0/state
# Output: running
```

### Step 3: Monitor Live Firmware Output via Trace Buffer

The firmware resource table declares a 4 KB trace buffer in DDR carveout (`0x48000000`). The Linux kernel remoteproc driver automatically exposes this to debugfs:

```bash
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

---

## 4. Hardware Serial Console (`S_UART0`)

The XuanTie co-processor has access to `S_UART0` at physical base `0x07080000`. This provides dedicated serial output independent of the main Linux console (`UART0` @ `0x02500000`):

```c
// In RISC-V firmware (main.c):
#define S_UART0_THR  (*(volatile uint32_t*)0x07080000)
#define S_UART0_LSR  (*(volatile uint32_t*)0x07080014)

void s_uart_putc(char c) {
    while ((S_UART0_LSR & (1 << 5)) == 0); // Wait for Transmit Holding Register Empty
    S_UART0_THR = (uint32_t)c;
}

void s_uart_puts(const char *s) {
    while (*s) s_uart_putc(*s++);
}
```

---

## 5. Stopping and Reloading Co-Processor Firmware

```bash
# Stop co-processor execution (asserts reset and gates MCU clocks)
echo stop > /sys/class/remoteproc/remoteproc0/state

# Load an updated ELF build
echo "new_firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
```

---

## 6. Physical JTAG Hardware Debugging

If interactive source-level debugging, hardware breakpoints, or single-stepping are needed:
1. Connect a hardware RISC-V debug probe (e.g. T-Head CK-Link, SEGGER J-Link with RISC-V support, or FT2232D) to the board's JTAG pins (`JTAG_MS`, `JTAG_CK`, `JTAG_DO`, `JTAG_DI`).
2. Run OpenOCD on your host PC pointing to the hardware adapter configuration:
   ```bash
   openocd -f interface/ftdi/jtag-lock-pick_tiny_2.cfg -f target/xuantie_e906.cfg
   ```
3. Connect GDB from your development machine:
   ```bash
   riscv-none-elf-gdb firmware.elf -ex "target remote localhost:3333"
   ```

---

## 7. Quick Reference

| Task | Command / Interface |
|---|---|
| Load & Start Firmware | `echo start > /sys/class/remoteproc/remoteproc0/state` |
| Stop Firmware | `echo stop > /sys/class/remoteproc/remoteproc0/state` |
| Read RemoteProc Trace | `cat /sys/kernel/debug/remoteproc/remoteproc0/trace0` |
| Serial Diagnostics | Dedicated `S_UART0` @ `0x07080000` (115200 baud) |
| Shared Memory Ring | Dedicated MCU SRAM (`0x3FFC0000`/`0x07280000`) / PubSRAM C (`0x00020000`) |
| Doorbell IPC | Hardware Mailbox @ `0x03003000` |
| Interactive Debugging | External JTAG probe + OpenOCD on host |

---

## Related Documentation

- [ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md)
- [RISCV_REMOTEPROC_GUIDE.md](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/common/RISCV_REMOTEPROC_GUIDE.md)
- [OpenOCD_DMEM_RISCV_Architecture.md](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/buildroot/OpenOCD_DMEM_RISCV_Architecture.md)
