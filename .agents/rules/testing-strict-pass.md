# Strict Hardware Test Verification Rule

## Core Principles
1. **Zero Tolerance for Fake Passes or Masked Failures**:
   - Tests MUST NEVER convert a failure, timeout, missing hardware response, or uninitialized co-processor into a `SKIP` or `PASS`.
   - If a test cannot communicate with the hardware, co-processor, or kernel node, it MUST FAIL with a clear diagnostic error (dmesg, remoteproc state, mailbox register dump).

2. **Real Co-Processor Verification**:
   - Every test for XuanTie E907 RISC-V and Cadence Tensilica HiFi4 DSP must verify that the core is actively loaded via `remoteproc`, running firmware, handling interrupts, and exchanging validated payloads (e.g. ping/pong, checksums, sequence numbers).
   - Round-trip latency (RTT) and throughput calculations must be derived from actual hardware timestamps and received packet counts.

3. **Standalone & Dual-Core Requirements**:
   - HiFi4 DSP standalone tests (e.g., `dsp-testMsgbox`) must run on real hardware and achieve 100% packet integrity and echo verification.
   - Dual-core concurrent tests must simultaneously ping both E907 (Ch 8/9) and DSP (Ch 4/5) with 100% verified responses from both cores.
   - If either core drops packets or fails to respond, the test suite must mark that profile as FAILED.

4. **Transparent Diagnostics**:
   - Always capture and report target `dmesg`, serial console output, and `/sys/class/remoteproc/` status upon any anomaly.
