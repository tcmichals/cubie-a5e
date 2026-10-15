# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 7
- **Latest Run**: Run #7 (2026-10-14 21:59:57)
- **Kernel Commit**: `39e8db786df7` | **Firmware/Tools Commit**: `deedec4`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox Hardware FIFO Pacing** | `last_tx_done returns count == 0 (done once the remote drained the FIFO)` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg uses synchronous poll()/select() with zero arbitrary sleep delays` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **FAIL** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |

### 🚀 Performance Improvements Detected

- 🟢 **Profile 1 C++ Rpmsg RTT: 517.14 -> 193.86 µs (-62.5%)**
- 🟢 **Profile 1 C++ Rpmsg Jitter: 597.00 -> 4.18 µs (-99.3%)**
- 🟢 **Profile 1 C++ Rpmsg Rate: 1507.10 -> 2996.90 msgs/s (+98.9%)**

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 47 tests | 47 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | 517.14 µs | 193.86 µs | `-323.28 µs (-62.5%)` | 🚀 True Speedup (Lower Latency) |
| Profile 1 C++ Rpmsg Jitter | 597.00 µs | 4.18 µs | `-592.82 µs (-99.3%)` | 🚀 True Speedup (Lower Latency) |
| Profile 1 C++ Rpmsg Rate | 1507.10 msgs/s | 2996.90 msgs/s | `+1489.80 msgs/s (+98.9%)` | 🚀 True Speedup (Higher Rate) |
| Profile 2 C++ Rpmsg RTT | 134.17 µs | 133.88 µs | `-0.29 µs (-0.2%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 14.02 µs | 13.91 µs | `-0.11 µs (-0.8%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 65072.60 msgs/s | 65382.80 msgs/s | `+310.20 msgs/s (+0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 63.55 MB/s | 63.85 MB/s | `+0.30 MB/s (+0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 181.09 µs | 178.37 µs | `-2.72 µs (-1.5%)` | 🟢 Invariant (Within Normal Jitter Band) |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #7 | 2026-10-14 21:59:57 | `39e8db786df7` | `deedec4` | **PASS** | 47/47 | 193.86 µs | 13.91 µs | 65,383/s |
| #6 | 2026-10-14 21:56:47 | `39e8db786df7` | `deedec4` | **FAIL** | 47/47 | 517.14 µs | 14.02 µs | 65,073/s |
| #5 | 2026-10-14 18:12:06 | `64c3c511452a` | `e676b78` | **FAIL** | 47/67 | 1242.68 µs | N/A | N/A |
| #4 | 2026-10-03 00:04:23 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 517.75 µs | 13.74 µs | 66,009/s |
| #3 | 2026-10-02 23:51:54 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 189.88 µs | 13.70 µs | 66,305/s |
| #2 | 2026-10-02 23:46:04 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 1242.43 µs | 13.75 µs | 66,138/s |
| #1 | 2026-10-02 23:38:50 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 200.16 µs | 13.85 µs | 65,244/s |

