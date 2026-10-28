# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 22
- **Latest Run**: Run #22 (2026-10-27 18:51:35)
- **Kernel Commit**: `b19a950e320c` | **Firmware/Tools Commit**: `bf729e7`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox TX Completion** | `last_tx_done returns count < SUN55I_FIFO_MAX (FIFO has room); DT selects timer or ISR when full` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **PASS** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |

### ⚠️ Statistically Significant Regressions (Exceeding Jitter Floor)

- 🔴 **KUnit In-Kernel Tests (Passed): 61/63 passed**

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 61 tests | 61 tests | `+0 tests` | 🔴 REGRESSION (Tests Failed or Missing!) |
| Profile 1 C++ Rpmsg RTT | 168.29 µs | 167.52 µs | `-0.77 µs (-0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 8.40 µs | 8.20 µs | `-0.21 µs (-2.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 5916.50 msgs/s | 5944.80 msgs/s | `+28.30 msgs/s (+0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 2 C++ Rpmsg RTT | 130.39 µs | 131.31 µs | `+0.92 µs (+0.7%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | N/A | 13.73 µs | `N/A` | ⚪ Baseline |
| Profile 3 C++ UIO Throughput | N/A | 8794.90 msgs/s | `N/A` | ⚪ Baseline |
| Profile 3 C++ UIO Bandwidth | N/A | 8.59 MB/s | `N/A` | ⚪ Baseline |
| Profile 3 Python UIO RTT | N/A | 35.53 µs | `N/A` | ⚪ Baseline |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #22 | 2026-10-27 18:51:35 | `b19a950e320c` | `bf729e7` | **PASS** | 61/63 | 167.52 µs | 13.73 µs | 8,795/s |
| #21 | 2026-10-27 18:38:08 | `b19a950e320c` | `bf729e7` | **FAIL** | 61/63 | 168.29 µs | N/A | N/A |
| #20 | 2026-10-20 21:59:02 | `d64ff2579e08` | `28eb1ce` | **PASS** | 54/54 | 166.14 µs | 13.71 µs | 8,988/s |
| #19 | 2026-10-20 21:23:31 | `d64ff2579e08` | `28eb1ce` | **PASS** | 51/51 | 165.90 µs | 13.93 µs | 8,981/s |
| #18 | 2026-10-17 14:43:43 | `82b64e947b5b` | `eb598cb` | **PASS** | 51/51 | 161.84 µs | 14.08 µs | 8,985/s |
| #17 | 2026-10-17 14:24:05 | `82b64e947b5b` | `eb598cb` | **PASS** | 51/51 | 162.71 µs | 13.69 µs | 8,789/s |
| #16 | 2026-10-17 13:06:22 | `82b64e947b5b` | `eb598cb` | **FAIL** | 51/51 | 172.69 µs | 13.74 µs | 8,973/s |
| #15 | 2026-10-17 12:33:23 | `82b64e947b5b` | `eb598cb` | **FAIL** | 51/51 | 169.68 µs | 14.45 µs | 8,985/s |
| #14 | 2026-10-17 11:51:49 | `82b64e947b5b` | `eb598cb` | **FAIL** | 51/51 | 167.59 µs | 14.35 µs | 8,986/s |
| #13 | 2026-10-16 21:01:29 | `82b64e947b5b` | `24890e7` | **FAIL** | 47/47 | 170.54 µs | 13.59 µs | 8,791/s |
| #12 | 2026-10-16 20:16:16 | `82b64e947b5b` | `24890e7` | **PASS** | 47/47 | 171.95 µs | 14.66 µs | 8,995/s |
| #11 | 2026-10-16 04:10:37 | `82b64e947b5b` | `2872b3e` | **PASS** | 47/47 | 167.21 µs | 13.65 µs | 8,823/s |
| #10 | 2026-10-15 19:13:49 | `71b7756b25bb` | `37fe2db` | **PASS** | 47/47 | 169.67 µs | 13.48 µs | 8,992/s |
| #9 | 2026-10-15 05:01:36 | `71b7756b25bb` | `37fe2db` | **FAIL** | 47/47 | N/A | 13.73 µs | 8,793/s |
| #8 | 2026-10-15 04:54:39 | `39e8db786df7` | `37fe2db` | **FAIL** | 47/47 | 166.47 µs | 13.51 µs | 8,991/s |
| #7 | 2026-10-14 21:59:57 | `39e8db786df7` | `deedec4` | **PASS** | 47/47 | 193.86 µs | 13.91 µs | 65,383/s |
| #6 | 2026-10-14 21:56:47 | `39e8db786df7` | `deedec4` | **FAIL** | 47/47 | 517.14 µs | 14.02 µs | 65,073/s |
| #5 | 2026-10-14 18:12:06 | `64c3c511452a` | `e676b78` | **FAIL** | 47/67 | 1242.68 µs | N/A | N/A |
| #4 | 2026-10-03 00:04:23 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 517.75 µs | 13.74 µs | 66,009/s |
| #3 | 2026-10-02 23:51:54 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 189.88 µs | 13.70 µs | 66,305/s |

