# Workspace Prompt 7: Markdown-Driven Architecture (Spec-Driven Development)

## 1. Principle
In this project ecosystem (AbstractX, cubie-a5e, inav-abstractx):
- **Markdown (`docs/`, `README.md`) is the Single Source of Truth (SSOT).**
- Architecture diagrams (Mermaid), hardware truth tables, memory maps, and protocol definitions live exclusively in Markdown.
- **Source code (`.hpp`, `.cpp`, `.S`, `.c`) must be kept minimal and lean.**
- Code comments should be short 1-line notes with direct cross-reference markers back to the markdown document:
  ```cpp
  // Spec: docs/PROMPT_MARKDOWN_DRIVEN_ARCHITECTURE.md
  ```

---

## 2. Universal Application Flow
The top-level flight/telemetry application (e.g. `FlightApp` / `inav-abstractx`) runs the exact same code across:
1. **Linux ARM64 Host (Cubie A5E PREEMPT_RT)**: Interconnects via `remoteproc` / Shared SRAM A3/C with the XuanTie E907 co-processor.
2. **Raspberry Pi Pico 2 W (RP2350)**: Core 1 runs the flight coroutines; Core 0 runs the I/O engine via SIO FIFO rings.
3. **Espressif ESP32-P4**: Core 1 runs the flight coroutines; Core 0 runs the I/O engine via IPC Mailbox rings.
4. **Desktop SITL Simulation**: Runs on simulated in-memory SPSC rings for unit tests.
