# Inter-Processor Communication (IPC) Memory Map

All communication between the Linux host (ARM64) and the XuanTie E907 co-processor is routed through a fixed window in **Shared System SRAM C (`0x07130000`)**:

---

## 1. Shared SRAM C Memory Offsets

| Region | Physical Offset | Size | Purpose |
| :--- | :--- | :--- | :--- |
| **Crash Dump Area** | `0x07130000` | 256 B | Magic `0x48535243` ('CRSH') + Fatal CPU registers (`mepc`, `mcause`, `mtval`) |
| **RISC-V TX Queue** | `0x07130100` | 16 KB | Lock-free SPSC queue for packets sent from RISC-V to Linux |
| **Linux TX Queue** | `0x07134100` | 16 KB | Lock-free SPSC queue for commands sent from Linux to RISC-V |
| **CTF Trace Buffer**| `0x07138100` | 32 KB | Barectf binary trace stream buffer |

---

## 2. 128-Byte Packet Format

Every message across the SPSC rings adheres to the 128-byte `IpcPacket` layout:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Magic (0x544C5049 'TLPI')                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Sequence Number         |          Packet Type          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Timestamp Microseconds (Low)               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Timestamp Microseconds (High)              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                   Payload Area (112 Bytes)                    |
|   (PCIe TLP Frames, Raw SPI Frames, Raw UART, Pigweed, Trace) |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Packet Types:
* `0x0010` — `PcieTlpToFpga`: Host Linux -> RISC-V -> FPGA (Memory Write/Read TLP)
* `0x0011` — `PcieTlpFromFpga`: FPGA -> RISC-V -> Host Linux (Completion/Interrupt TLP)
* `0x0020` — `RawSpiTransfer`: Raw SPI transfers (IMU, external sensors)
* `0x0030` — `RawUartStream`: High-speed UART2 serial streams
* `0x0040` — `PigweedLog`: 4-byte tokenized logs
* `0x0050` — `BarectfTrace`: CTF binary execution trace chunks

