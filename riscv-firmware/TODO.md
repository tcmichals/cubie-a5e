# RISC-V Firmware & RemoteProc Driver: Roadmap & Testing TODOs

This document tracks active accomplishments, hardware proofs, and upcoming testing/enhancement tasks for the XuanTie E907 / E902 co-processor firmware and Linux `sunxi_rproc` driver.

---

## 1. Verified Silicon Milestones (Radxa Cubie A5E / Allwinner T527)

- [x] **Native Clock & Reset Subsystem Control**: Replaced all user-space `/dev/mem` register pokes with Linux kernel `clk` and `reset` frameworks.
- [x] **ELF Bootstrap & Dynamic Switching**: Verified dynamic `start` -> `stop` -> `start` lifecycle and firmware switching via sysfs.
- [x] **Debugfs Trace Section Parsing**: Live trace logging through `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
- [x] **VirtIO RPMsg over DDR DRAM**: Linux CMA coherent DDR buffers (`0xf2f80000`), mailbox doorbells, PREEMPT_RT deferred workqueue, Name Service announcement, and `/dev/rpmsg0` communication.
- [x] **Standardized 512-Byte Apples-to-Apples Benchmarks**:
  - `ping_shm` (Direct SRAM SPSC): **14.59 $\mu\text{s}$ avg**, 63,022 msgs/s, 61.54 MB/s.
  - `ping_dram` (Hybrid SRAM/DDR Carveout): **191.84 $\mu\text{s}$ avg**, 4,710 msgs/s, 4.60 MB/s.
  - `ping_rpmsg` (Linux VirtIO RPMsg): **175.64 $\mu\text{s}$ avg**, 5,671 msgs/s, 5.37 MB/s.
- [x] **Exception Trapping & Autopsy Dump**: Verified Machine-mode trap vector captures register state to SRAM and halts in `wfi` without ARM host Linux kernel panic.
- [x] **Automated Test Suite**: All 4 tests in `python3 /usr/bin/run_tests.py` passing on physical silicon.
- [x] **Kernel Patch Validation Gate**: Automated `tools/validate_kernel_patches.py` checking clean dry-runs against Linux 7.1 with 0 `checkpatch.pl` errors.

---

## 2. Additional Testing Scope & Driver Enhancements (TODO)

### Task 1: Pure On-Chip SRAM VirtIO RPMsg Benchmark (`testPingRpmsgSram`)
* **Goal**: Configure VirtIO vrings and payload buffers to reside directly in on-chip SRAM Space 1 (`0x40000000` Core DA / `0x072C0000` Host PA) instead of dynamic DDR CMA allocation (`da = FW_RSC_ADDR_ANY`).
* **Purpose**: Establish a complete 4-tier architectural performance comparison:
  1. Bare-metal SRAM polling (`ping_shm`): **14.59 $\mu\text{s}$**
  2. Linux VirtIO RPMsg in On-Chip SRAM (Projected): **~40 – 50 $\mu\text{s}$**
  3. Linux VirtIO RPMsg in DDR CMA (`ping_rpmsg`): **175.64 $\mu\text{s}$**
  4. Hybrid SRAM / DDR Carveout (`ping_dram`): **191.84 $\mu\text{s}$**
* **Action Items**:
  - [ ] Update `resource_table.c` to declare explicit `.da = 0x40000000` in SRAM Space 1 for Vring 0, Vring 1, and VirtIO payload message buffers.
  - [ ] Ensure `sunxi_rproc.c` handles SRAM mapping without invoking `dma_alloc_coherent()`.
  - [ ] Execute `ping_rpmsg -n 1000 -s 496` over SRAM VirtIO and measure latency, jitter, throughput, and bandwidth on physical silicon.
  - [ ] Update comparison tables in `tests.md` and Part 2/3 articles.

### Task 2: Automatic Crash Recovery (`rproc_report_crash`)
* **Goal**: Enable automatic kernel detection and core restart when the E907 suffers a hardware exception or watchdog timeout.
* **Current State**: `testCrash.elf` captures register autopsy to SRAM and halts in low-power `wfi`. Automatic recovery is disabled (`echo disabled > recovery`) to permit manual post-mortem inspection.
* **Action Items**:
  - [ ] Implement an IRQ or Mailbox alert from the E907 exception handler back to the ARM host.
  - [ ] Connect the notification to `rproc_report_crash(priv->rproc, RPROC_FATAL_ERROR)` in `sunxi_rproc.c`.
  - [ ] Verify that with `recovery = enabled`, Linux automatically reboots the E907 and restarts the default firmware.

### Task 3: System Power Management (`pm_runtime` & Suspend/Resume)
* **Goal**: Enable SoC deep sleep (suspend-to-RAM / S3) without crashing or corrupting running E907 firmware.
* **Current State**: System suspend with active remoteproc is untested.
* **Action Items**:
  - [ ] Implement `suspend` and `resume` callbacks in `sunxi_rproc.c`.
  - [ ] Test `echo mem > /sys/power/state` while E907 is running.
  - [ ] Verify that on-chip SRAM retention preserves firmware state or cleanly reboots post-wake.

### Task 4: Multi-Channel Concurrency & High-Load Stress Testing
* **Goal**: Stress test VirtIO RPMsg under heavy concurrent workload and multi-threading.
* **Current State**: Benchmarked with a single active channel (`rpmsg-ping-channel`) and single-threaded ping loops.
* **Action Items**:
  - [ ] Implement multi-endpoint firmware announcing multiple RPMsg channels (e.g. `rpmsg-telemetry`, `rpmsg-ctrl`, `rpmsg-data`).
  - [ ] Build a multi-threaded stress utility running 10+ concurrent threads writing and reading `/dev/rpmsg0`..`/dev/rpmsgN`.
  - [ ] Measure lock contention, PREEMPT_RT latency degradation, and vring buffer exhaustion.

### Task 5: Cadence Tensilica HiFi4 DSP RemoteProc Bring-Up
* **Goal**: Extend RemoteProc support to the secondary DSP co-processor on Allwinner T527 / A527.
* **Current State**: Driver currently targets XuanTie E907 core.
* **Action Items**:
  - [ ] Add HiFi4 DSP memory window mapping (`0x00020000`, 320 KB) and clock/reset controls.
  - [ ] Add DT binding for DSP remoteproc instance.
  - [ ] Compile and validate minimal DSP bring-up ELF.
