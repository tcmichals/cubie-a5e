# Cadence Tensilica HiFi4 Audio DSP Testing Framework
**Platform**: Allwinner T527 / A527 (Radxa Cubie A5E)  
**Core**: Cadence Tensilica HiFi4 Audio DSP @ 600 MHz  

---

## 1. Overview & Architecture
This directory contains lightweight test firmware and toolchain configurations leveraging the [YuzukiHD/FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) architecture and toolchain for validating:
1. **Remoteproc Core Manager**: Parsing and loading ELF binaries into the dedicated 4 MB execution window (`0x40100000`), releasing DSP clock/reset gating, and lifecycle start/stop.
2. **Trace0 Debug Buffer**: Exporting real-time string diagnostics from the DSP via `RSC_TRACE` in the ELF `.resource_table` section to `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
3. **Hardware Message Box (sun55i-msgbox)**: Validating bidirectional IPC FIFO and interrupt delivery between ARM Cortex-A55 and the HiFi4 DSP over Mailbox Channels 4 & 5 using the low-level hardware routines adapted from YuzukiHD.
4. **Toolchain**: Built using the crosstool-NG GCC 10.3.0 `xtensa-hifi4-elf-` compiler located at `~/.tools/xtensa-hifi4-gcc/bin/`.

---

## 2. Memory Map & Layout
| Region | Base Address | Size | Description |
| :--- | :--- | :--- | :--- |
| **`coproc_shm`** | `0x40000000` | 1 MB | Zero-copy shared DMA buffer pool (`shared-dma-pool`, `no-map`) |
| **`coproc_firmware`** | `0x40100000` | 4 MB | DSP Firmware execution segment (`no-map`) |
| **`PubSRAM C`** | `0x00020000` | 128 KB | On-chip DSP local instruction/data RAM |
| **`DSP Local IRAM`** | `0x00400000` | 64 KB | Dedicated HiFi4 instruction memory |
| **`DSP Local DRAM0/1`** | `0x00420000` | 64 KB | Dedicated HiFi4 data memory banks |

---

## 3. Applications
- **`apps/testBasic`**:
  - Tests remoteproc ELF parsing, loading, and core start/stop lifecycle.
  - Initializes `trace0` buffer and outputs boot telemetry visible via:
    ```bash
    cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
    ```
- **`apps/testMsgbox`**:
  - Validates hardware mailbox IPC.
  - Listens for incoming 32-bit words on Mailbox Channel 4 (ARM -> DSP).
  - Logs arrival to `trace0`.
  - Responds back on Mailbox Channel 5 (DSP -> ARM) with `0x504F4E47` (`"PONG"`).

---

## 4. Hardware Overlays
To enable on the Radxa Cubie A5E:
```ini
# In /boot/config.txt
dtoverlay=cubie-a5e-mailbox-test cubie-a5e-dsp
```

---

## 5. Target Validation
Run the target validation script:
```bash
/usr/bin/validate_coproc.sh
```
Or view the trace output in real time:
```bash
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```
