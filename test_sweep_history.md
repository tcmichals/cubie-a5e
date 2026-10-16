# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 10
- **Latest Run**: Run #10 (2026-10-15 19:13:49)
- **Kernel Commit**: `71b7756b25bb` | **Firmware/Tools Commit**: `37fe2db`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox Hardware FIFO Pacing** | `last_tx_done returns count == 0 (done once the remote drained the FIFO)` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **PASS** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |

### 🚀 Performance Improvements Detected

- 🟢 **Profile 3 Python UIO RTT: 35.58 -> 31.95 µs (-10.2%)**

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 47 tests | 47 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | N/A | 169.67 µs | `N/A` | ⚪ Baseline |
| Profile 1 C++ Rpmsg Jitter | N/A | 7.92 µs | `N/A` | ⚪ Baseline |
| Profile 1 C++ Rpmsg Rate | N/A | 5861.40 msgs/s | `N/A` | ⚪ Baseline |
| Profile 2 C++ Rpmsg RTT | 133.69 µs | 132.96 µs | `-0.72 µs (-0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.73 µs | 13.48 µs | `-0.25 µs (-1.8%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 8792.90 msgs/s | 8992.00 msgs/s | `+199.10 msgs/s (+2.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 8.59 MB/s | 8.78 MB/s | `+0.19 MB/s (+2.2%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 35.58 µs | 31.95 µs | `-3.63 µs (-10.2%)` | 🚀 True Speedup (Lower Latency) |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #10 | 2026-10-15 19:13:49 | `71b7756b25bb` | `37fe2db` | **PASS** | 47/47 | 169.67 µs | 13.48 µs | 8,992/s |
| #9 | 2026-10-15 05:01:36 | `71b7756b25bb` | `37fe2db` | **FAIL** | 47/47 | N/A | 13.73 µs | 8,793/s |
| #8 | 2026-10-15 04:54:39 | `39e8db786df7` | `37fe2db` | **FAIL** | 47/47 | 166.47 µs | 13.51 µs | 8,991/s |
| #7 | 2026-10-14 21:59:57 | `39e8db786df7` | `deedec4` | **PASS** | 47/47 | 193.86 µs | 13.91 µs | 65,383/s |
| #6 | 2026-10-14 21:56:47 | `39e8db786df7` | `deedec4` | **FAIL** | 47/47 | 517.14 µs | 14.02 µs | 65,073/s |
| #5 | 2026-10-14 18:12:06 | `64c3c511452a` | `e676b78` | **FAIL** | 47/67 | 1242.68 µs | N/A | N/A |
| #4 | 2026-10-03 00:04:23 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 517.75 µs | 13.74 µs | 66,009/s |
| #3 | 2026-10-02 23:51:54 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 189.88 µs | 13.70 µs | 66,305/s |
| #2 | 2026-10-02 23:46:04 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 1242.43 µs | 13.75 µs | 66,138/s |
| #1 | 2026-10-02 23:38:50 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 200.16 µs | 13.85 µs | 65,244/s |

