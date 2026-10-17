# Strict Hardware Test Verification Rule

## Core Principles
1. **Zero Tolerance for Fake Passes or Masked Failures**:
   - Tests MUST NEVER convert a failure, timeout, missing hardware response, or uninitialized co-processor into a `SKIP` or `PASS`.
   - If a test cannot communicate with the hardware, co-processor, or kernel node, it MUST FAIL with a clear diagnostic error (dmesg, remoteproc state, mailbox register dump).

2. **Real Co-Processor Verification**:
   - Every test for XuanTie E906 RISC-V and Cadence Tensilica HiFi4 DSP must verify that the core is actively loaded via `remoteproc`, running firmware, handling interrupts, and exchanging validated payloads (e.g. ping/pong, checksums, sequence numbers).
   - Round-trip latency (RTT) and throughput calculations must be derived from actual hardware timestamps and received packet counts.

3. **Standalone & Dual-Core Requirements**:
   - HiFi4 DSP standalone tests (e.g., `dsp-testMsgbox`) must run on real hardware and achieve 100% packet integrity and echo verification.
   - Dual-core concurrent tests must simultaneously ping both E906 (channel 9, decision D11) and DSP (Ch 4/5) with 100% verified responses from both cores.
   - If either core drops packets or fails to respond, the test suite must mark that profile as FAILED.

4. **Transparent Diagnostics**:
   - Always capture and report target `dmesg`, serial console output, and `/sys/class/remoteproc/` status upon any anomaly.

5. **Deploying a Kernel to the Board** (2026-10-16: a mismatched Image broke Ethernet and WiFi):
   - The board's drivers for Ethernet (`dwmac-sun55i`), WiFi (`cfg80211`, `aic8800_*`) and others are
     modules in `/lib/modules/<release>` on the rootfs, built by Buildroot as release `7.1.0`.
   - A kernel built straight from the `linux-cubie` git tree gets `7.1.0-g<hash>-dirty`
     (`CONFIG_LOCALVERSION_AUTO`), finds no modules and boots without network. Build out of tree with
     Buildroot's `.config`, `scripts/config --disable LOCALVERSION_AUTO`, and `make ... LOCALVERSION=`.
   - Before copying an `Image` to `/boot`: `include/config/kernel.release` must equal a directory in the
     board's `/lib/modules/`, and the kernel's vermagic must equal a board module's
     (`modinfo -F vermagic .../aic8800_fdrv.ko`). If not, do not deploy.
   - Keep the board's previous `Image` and DTBs in `/root/` (not `/boot`, it is too small) so it can be
     restored from the serial console. After boot, check `uname -r`, `eth0` and `wlan0` before testing.
