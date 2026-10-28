# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 24
- **Latest Run**: Run #24 (2026-10-28 17:31:20)
- **Kernel Commit**: `8671beca6667` | **Firmware/Tools Commit**: `3d9ad94`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox TX Completion** | `last_tx_done returns count < SUN55I_FIFO_MAX (FIFO has room); DT selects timer or ISR when full` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **PASS** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |

### 🚀 Performance Improvements Detected

- 🟢 **Profile 3 Python UIO RTT: 35.51 -> 32.57 µs (-8.3%)**

## Range Check Against the Last 10 Passing Runs

SLOW fails the sweep; FAST is reported only; NEW has fewer than 3 earlier runs.

| Metric | Current | Median | Range | Runs | Verdict |
| :--- | ---: | ---: | :---: | :---: | :---: |
| mbx_isr_freertos_p99_us | 221.33 | - | - | 1 | **NEW** |
| mbx_isr_freertos_rate_msgs_s | 5308.90 | - | - | 1 | **NEW** |
| mbx_isr_freertos_rtt_us | 187.64 | - | - | 1 | **NEW** |
| mbx_isr_ping_p99_us | 195.00 | - | - | 1 | **NEW** |
| mbx_isr_ping_rate_msgs_s | 5980.00 | - | - | 1 | **NEW** |
| mbx_isr_ping_rtt_us | 166.57 | - | - | 1 | **NEW** |
| mbx_isr_stress_rate_msgs_s | 11569.70 | - | - | 1 | **NEW** |
| mbx_timer_freertos_p99_us | 230.25 | - | - | 1 | **NEW** |
| mbx_timer_freertos_rate_msgs_s | 5104.10 | - | - | 1 | **NEW** |
| mbx_timer_freertos_rtt_us | 195.12 | - | - | 1 | **NEW** |
| mbx_timer_ping_p99_us | 194.75 | - | - | 1 | **NEW** |
| mbx_timer_ping_rate_msgs_s | 5958.80 | - | - | 1 | **NEW** |
| mbx_timer_ping_rtt_us | 167.16 | - | - | 1 | **NEW** |
| mbx_timer_stress_rate_msgs_s | 11415.40 | - | - | 1 | **NEW** |
| p1_dram_rtt_us | 201.84 | 194.24 | 174.81 .. 213.66 | 10 | **OK** |
| p1_dram_w16_rate_msgs_s | 5002.00 | 5089.95 | 4580.96 .. 5598.95 | 8 | **OK** |
| p1_rpmsg_jitter_us | 8.61 | 8.84 | 0.00 .. 17.68 | 8 | **OK** |
| p1_rpmsg_rate_msgs_s | 5933.40 | 5967.75 | 5370.98 .. 6564.52 | 8 | **OK** |
| p1_rpmsg_rtt_us | 167.77 | 166.68 | 150.01 .. 183.35 | 8 | **OK** |
| p1_stress_rate_msgs_s | 11415.30 | 11518.75 | 10366.88 .. 12670.62 | 8 | **OK** |
| p2_rpmsg_rate_msgs_s | 7606.90 | 7617.50 | 6855.75 .. 8379.25 | 8 | **OK** |
| p2_rpmsg_rtt_us | 130.73 | 130.92 | 117.83 .. 144.01 | 10 | **OK** |
| p2_stress_rate_msgs_s | 20397.20 | 20392.35 | 18353.11 .. 22431.58 | 8 | **OK** |
| p3_shm_bandwidth_mb_s | 8.77 | 8.77 | 7.89 .. 9.65 | 8 | **OK** |
| p3_shm_rate_msgs_s | 8981.90 | 8982.95 | 8084.66 .. 9881.25 | 8 | **OK** |
| p3_shm_rtt_us | 13.91 | 13.72 | 11.72 .. 15.72 | 8 | **OK** |
| p3_uio_irq_rtt_us | 59.76 | 59.23 | 53.31 .. 65.16 | 8 | **OK** |
| p3_uio_py_rate_msgs_s | 160.10 | 160.50 | 120.38 .. 200.62 | 8 | **OK** |
| p3_uio_py_rtt_us | 32.57 | 32.38 | 24.28 .. 40.47 | 8 | **OK** |

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 63 tests | 63 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | 168.39 µs | 167.77 µs | `-0.62 µs (-0.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 9.29 µs | 8.61 µs | `-0.68 µs (-7.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 5914.30 msgs/s | 5933.40 msgs/s | `+19.10 msgs/s (+0.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 2 C++ Rpmsg RTT | 130.02 µs | 130.73 µs | `+0.72 µs (+0.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.64 µs | 13.91 µs | `+0.27 µs (+2.0%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 8986.60 msgs/s | 8981.90 msgs/s | `-4.70 msgs/s (-0.1%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 8.78 MB/s | 8.77 MB/s | `-0.01 MB/s (-0.1%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 35.51 µs | 32.57 µs | `-2.94 µs (-8.3%)` | 🚀 True Speedup (Lower Latency) |

## Every Run Against the Passing Runs Before It

SLOW: slower or lower rate than the band (fails the sweep since the check was added); FAST: better than the band.

| Run | Date | Kernel | Status | In range | Out of range (value vs median of last 10 PASS) |
| ---: | :--- | :--- | :---: | ---: | :--- |
| #1 | 2026-10-02 23:38 | `b861276e3e76` | PASS | 0/0 | - |
| #2 | 2026-10-02 23:46 | `b861276e3e76` | PASS | 0/0 | - |
| #3 | 2026-10-02 23:51 | `b861276e3e76` | PASS | 0/0 | - |
| #4 | 2026-10-03 00:04 | `b861276e3e76` | PASS | 2/2 | - |
| #5 | 2026-10-14 18:12 | `64c3c511452a` | FAIL | 2/2 | - |
| #6 | 2026-10-14 21:56 | `39e8db786df7` | FAIL | 2/2 | - |
| #7 | 2026-10-14 21:59 | `39e8db786df7` | PASS | 2/2 | - |
| #8 | 2026-10-15 04:54 | `39e8db786df7` | FAIL | 1/1 | - |
| #9 | 2026-10-15 05:01 | `71b7756b25bb` | FAIL | 1/1 | - |
| #10 | 2026-10-15 19:13 | `71b7756b25bb` | PASS | 2/2 | - |
| #11 | 2026-10-16 04:10 | `82b64e947b5b` | PASS | 2/2 | - |
| #12 | 2026-10-16 20:16 | `82b64e947b5b` | PASS | 2/2 | - |
| #13 | 2026-10-16 21:01 | `82b64e947b5b` | FAIL | 15/15 | - |
| #14 | 2026-10-17 11:51 | `82b64e947b5b` | FAIL | 15/15 | - |
| #15 | 2026-10-17 12:33 | `82b64e947b5b` | FAIL | 15/15 | - |
| #16 | 2026-10-17 13:06 | `82b64e947b5b` | FAIL | 15/15 | - |
| #17 | 2026-10-17 14:24 | `82b64e947b5b` | PASS | 15/15 | - |
| #18 | 2026-10-17 14:43 | `82b64e947b5b` | PASS | 15/15 | - |
| #19 | 2026-10-20 21:23 | `d64ff2579e08` | PASS | 15/15 | - |
| #20 | 2026-10-20 21:59 | `d64ff2579e08` | PASS | 15/15 | - |
| #21 | 2026-10-27 18:38 | `b19a950e320c` | FAIL | 9/9 | - |
| #22 | 2026-10-27 18:51 | `b19a950e320c` | PASS | 15/15 | - |
| #23 | 2026-10-28 17:24 | `8671beca6667` | FAIL | 15/15 | - |
| #24 | 2026-10-28 17:31 | `8671beca6667` | PASS | 15/15 | - |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #24 | 2026-10-28 17:31:20 | `8671beca6667` | `3d9ad94` | **PASS** | 63/63 | 167.77 µs | 13.91 µs | 8,982/s |
| #23 | 2026-10-28 17:24:22 | `8671beca6667` | `3d9ad94` | **FAIL** | 63/63 | 168.39 µs | 13.64 µs | 8,987/s |
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

