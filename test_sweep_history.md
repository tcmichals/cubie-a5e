# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 11
- **Latest Run**: Run #11 (2026-10-16 04:10:37)
- **Kernel Commit**: `82b64e947b5b` | **Firmware/Tools Commit**: `2872b3e`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox Hardware FIFO Pacing** | `last_tx_done returns count == 0 (done once the remote drained the FIFO)` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **PASS** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **PASS** |

### 🟢 Performance is Statistically Invariant & Stable

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 47 tests | 47 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | 169.67 µs | 167.21 µs | `-2.46 µs (-1.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 7.92 µs | 8.17 µs | `+0.25 µs (+3.1%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 5861.40 msgs/s | 5944.60 msgs/s | `+83.20 msgs/s (+1.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 2 C++ Rpmsg RTT | 132.96 µs | 130.53 µs | `-2.43 µs (-1.8%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.48 µs | 13.65 µs | `+0.17 µs (+1.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 8992.00 msgs/s | 8823.20 msgs/s | `-168.80 msgs/s (-1.9%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 8.78 MB/s | 8.62 MB/s | `-0.16 MB/s (-1.8%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 31.95 µs | 32.38 µs | `+0.43 µs (+1.3%)` | 🟢 Invariant (Within Normal Jitter Band) |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #11 | 2026-10-16 04:10:37 | `82b64e947b5b` | `2872b3e` | **PASS** | 47/47 | 167.21 µs | 13.65 µs | 8,823/s |
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

