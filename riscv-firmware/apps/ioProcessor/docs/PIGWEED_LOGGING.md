# Google Pigweed Tokenized Logging

This project uses **Google Pigweed (`pw_tokenizer`)** to eliminate string storage overhead and minimize IPC bandwidth.

---

## 1. How Tokenized Logging Works

1. During compilation, the `PW_TOKENIZE_STRING("...")` macro calculates a 32-bit hash of the string literal at compile time.
2. The firmware transmits only the **4-byte token** and packed varint arguments inside a 128-byte `IpcPacket`.
3. The Linux companion daemon (`io-bridge`) looks up the 32-bit token in its database and prints the full formatted string with nanosecond timestamp.

---

## 2. Viewing Real-Time Logs

Run the `io-bridge` daemon on the Cubie A5E:

```bash
# Run host bridge daemon (requires root permissions for /dev/mem)
sudo ./io-bridge
```

Example Output:
```text
[IPC Bridge] Successfully mapped Shared SRAM C @ 0x7130000
[Main] Bridge active. Listening for telemetry, logs, and traces...
[0.002150 s] [RISC-V] XuanTie E907 Hardware I/O & PCIe TLP Processor Booting...
[0.002240 s] [RISC-V] AbstractX C++20 Coroutine Scheduler Initialized
[0.002310 s] [RISC-V] FPGA PCIe TLP Dual-SPI Bridge Task Initialized
[0.002380 s] [RISC-V] IMU Sensor Acquisition Task Initialized
[0.002450 s] [RISC-V] UART2 Serial Stream Ingestion Task Initialized
[0.002520 s] [RISC-V] Entering Hard Real-Time I/O Event Loop
[I/O Throughput] PCIe TLPs: 500 pkts/s | IMU SPI: 1000 pkts/s | UART: 50 pkts/s
```
