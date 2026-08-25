# Barectf Common Trace Format (CTF) Guide

This guide describes how to record, ingest, and inspect nanosecond-level execution traces from the XuanTie E907 RISC-V co-processor.

---

## 1. How Traces are Emitted

1. As AbstractX coroutines switch tasks or execute SPI/UART transfers, the firmware calls `TraceManager::trace_spi()` or `TraceManager::trace_task_switch()`.
2. The Barectf engine writes binary event records into the 32 KB CTF buffer in **Shared SRAM C (`0x07138100`)**.
3. When a buffer slice is full or flushed, the Linux daemon (`io-bridge`) ingests the chunk and appends it to `io_trace.ctf`.

---

## 2. Inspecting Traces on Linux

You can view human-readable timelines using **Babeltrace 2**:

```bash
# Install babeltrace2
sudo apt-get install babeltrace2

# Print human-readable trace events
babeltrace2 io_trace.ctf
```

To view rich graphical timelines with task execution lanes and latency histograms, open `io_trace.ctf` in **Eclipse Trace Compass**.
