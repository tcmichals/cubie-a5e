# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 27
- **Latest Run**: Run #27 (2026-10-29 06:29:10)
- **Kernel Commit**: `0a77050147bd` | **Firmware/Tools Commit**: `15a83d5`
- **Latest Overall Status**: **PASS**

## Engineering Invariants & Code Hygiene Audit

| Subsystem Invariant | Formal Design Rule | Component | Status |
| :--- | :--- | :--- | :---: |
| **RemoteProc VirtIO Doorbell Contract** | `priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)` | `drivers/remoteproc/sunxi_rproc.c` | **PASS** |
| **Mailbox TX Completion** | `last_tx_done returns count < SUN55I_FIFO_MAX (FIFO has room); DT selects timer or ISR when full` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |
| **Zero-Sleep Benchmark Protocol** | `ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)` | `apps/testPingRpmsg/linux/ping_rpmsg.cpp` | **PASS** |
| **PREEMPT_RT Hardirq Execution Bound** | `sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX` | `drivers/mailbox/sun55i-msgbox.c` | **FAIL** |

### ⚠️ Statistically Significant Regressions (Exceeding Jitter Floor)

- 🔴 **Profile 3 Python UIO RTT: 32.57 -> 35.53 µs (+9.1%)**

## Range Check Against the Last 10 Passing Runs

SLOW fails the sweep; FAST is reported only; NEW has fewer than 3 earlier runs.

| Metric | Current | Median | Range | Runs | Verdict |
| :--- | ---: | ---: | :---: | :---: | :---: |
| mbx_isr_freertos_p99_us | 217.92 | - | - | 2 | **NEW** |
| mbx_isr_freertos_rate_msgs_s | 5313.00 | - | - | 2 | **NEW** |
| mbx_isr_freertos_rtt_us | 187.52 | - | - | 2 | **NEW** |
| mbx_isr_ping_p99_us | 192.29 | - | - | 2 | **NEW** |
| mbx_isr_ping_rate_msgs_s | 5965.40 | - | - | 2 | **NEW** |
| mbx_isr_ping_rtt_us | 166.95 | - | - | 2 | **NEW** |
| mbx_isr_stress_rate_msgs_s | 11521.40 | - | - | 2 | **NEW** |
| mbx_timer_freertos_p99_us | 227.54 | - | - | 2 | **NEW** |
| mbx_timer_freertos_rate_msgs_s | 5201.00 | - | - | 2 | **NEW** |
| mbx_timer_freertos_rtt_us | 191.50 | - | - | 2 | **NEW** |
| mbx_timer_ping_p99_us | 193.25 | - | - | 2 | **NEW** |
| mbx_timer_ping_rate_msgs_s | 5977.60 | - | - | 2 | **NEW** |
| mbx_timer_ping_rtt_us | 166.59 | - | - | 2 | **NEW** |
| mbx_timer_stress_rate_msgs_s | 11570.30 | - | - | 2 | **NEW** |
| p1_dram_rtt_us | 192.20 | 194.99 | 175.49 .. 214.49 | 10 | **OK** |
| p1_dram_w16_rate_msgs_s | 5123.00 | 5083.10 | 4574.79 .. 5591.41 | 9 | **OK** |
| p1_rpmsg_jitter_us | 9.29 | 8.74 | 0.00 .. 17.49 | 9 | **OK** |
| p1_rpmsg_rate_msgs_s | 5966.70 | 5944.80 | 5350.32 .. 6539.28 | 9 | **OK** |
| p1_rpmsg_rtt_us | 166.78 | 167.21 | 150.49 .. 183.94 | 9 | **OK** |
| p1_stress_rate_msgs_s | 11571.00 | 11514.10 | 10362.69 .. 12665.51 | 9 | **OK** |
| p2_rpmsg_rate_msgs_s | 7652.90 | 7610.00 | 6849.00 .. 8371.00 | 9 | **OK** |
| p2_rpmsg_rtt_us | 129.99 | 130.63 | 117.57 .. 143.69 | 10 | **OK** |
| p2_stress_rate_msgs_s | 20447.20 | 20393.00 | 18353.70 .. 22432.30 | 9 | **OK** |
| p3_shm_bandwidth_mb_s | 8.77 | 8.77 | 7.89 .. 9.65 | 9 | **OK** |
| p3_shm_rate_msgs_s | 8983.60 | 8981.90 | 8083.71 .. 9880.09 | 9 | **OK** |
| p3_shm_rtt_us | 13.59 | 13.73 | 11.73 .. 15.73 | 9 | **OK** |
| p3_uio_irq_rtt_us | 60.12 | 59.30 | 53.37 .. 65.23 | 9 | **OK** |
| p3_uio_py_rate_msgs_s | 158.70 | 160.50 | 120.38 .. 200.62 | 9 | **OK** |
| p3_uio_py_rtt_us | 35.53 | 32.38 | 24.29 .. 40.48 | 9 | **OK** |

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Statistical Assessment |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 63 tests | 63 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | 167.77 µs | 166.78 µs | `-0.99 µs (-0.6%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 8.61 µs | 9.29 µs | `+0.68 µs (+7.9%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 5933.40 msgs/s | 5966.70 msgs/s | `+33.30 msgs/s (+0.6%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 2 C++ Rpmsg RTT | 130.73 µs | 129.99 µs | `-0.74 µs (-0.6%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.91 µs | 13.59 µs | `-0.32 µs (-2.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 8981.90 msgs/s | 8983.60 msgs/s | `+1.70 msgs/s (+0.0%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 8.77 MB/s | 8.77 MB/s | `+0.00 MB/s (+0.0%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 32.57 µs | 35.53 µs | `+2.96 µs (+9.1%)` | ⚠️ REGRESSION (Exceeds Jitter Noise Floor) |

## Every Run Against the Passing Runs Before It

SLOW: slower or lower rate than the band (fails the sweep since the check was added); FAST: better than the band.

| Run | Date | Kernel | Flavor | Status | In range | Out of range (value vs median of last 10 PASS) |
| ---: | :--- | :--- | :--- | :---: | ---: | :--- |
| #1 | 2026-10-02 23:38 | `b861276e3e76` | rt | PASS | 0/0 | - |
| #2 | 2026-10-02 23:46 | `b861276e3e76` | rt | PASS | 0/0 | - |
| #3 | 2026-10-02 23:51 | `b861276e3e76` | rt | PASS | 0/0 | - |
| #4 | 2026-10-03 00:04 | `b861276e3e76` | rt | PASS | 2/2 | - |
| #5 | 2026-10-14 18:12 | `64c3c511452a` | rt | FAIL | 2/2 | - |
| #6 | 2026-10-14 21:56 | `39e8db786df7` | rt | FAIL | 2/2 | - |
| #7 | 2026-10-14 21:59 | `39e8db786df7` | rt | PASS | 2/2 | - |
| #8 | 2026-10-15 04:54 | `39e8db786df7` | rt | FAIL | 1/1 | - |
| #9 | 2026-10-15 05:01 | `71b7756b25bb` | rt | FAIL | 1/1 | - |
| #10 | 2026-10-15 19:13 | `71b7756b25bb` | rt | PASS | 2/2 | - |
| #11 | 2026-10-16 04:10 | `82b64e947b5b` | rt | PASS | 2/2 | - |
| #12 | 2026-10-16 20:16 | `82b64e947b5b` | rt | PASS | 2/2 | - |
| #13 | 2026-10-16 21:01 | `82b64e947b5b` | rt | FAIL | 15/15 | - |
| #14 | 2026-10-17 11:51 | `82b64e947b5b` | rt | FAIL | 15/15 | - |
| #15 | 2026-10-17 12:33 | `82b64e947b5b` | rt | FAIL | 15/15 | - |
| #16 | 2026-10-17 13:06 | `82b64e947b5b` | rt | FAIL | 15/15 | - |
| #17 | 2026-10-17 14:24 | `82b64e947b5b` | rt | PASS | 15/15 | - |
| #18 | 2026-10-17 14:43 | `82b64e947b5b` | rt | PASS | 15/15 | - |
| #19 | 2026-10-20 21:23 | `d64ff2579e08` | rt | PASS | 15/15 | - |
| #20 | 2026-10-20 21:59 | `d64ff2579e08` | rt | PASS | 15/15 | - |
| #21 | 2026-10-27 18:38 | `b19a950e320c` | rt | FAIL | 9/9 | - |
| #22 | 2026-10-27 18:51 | `b19a950e320c` | rt | PASS | 15/15 | - |
| #23 | 2026-10-28 17:24 | `8671beca6667` | rt | FAIL | 15/15 | - |
| #24 | 2026-10-28 17:31 | `8671beca6667` | rt | PASS | 15/15 | - |
| #25 | 2026-10-28 18:50 | `8671beca6667` | preempt | PASS | 0/0 | - |
| #26 | 2026-10-28 19:21 | `8671beca6667` | preempt+lockdep | PASS | 0/0 | - |
| #27 | 2026-10-29 06:29 | `0a77050147bd` | rt | PASS | 15/15 | - |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #27 | 2026-10-29 06:29:10 | `0a77050147bd` | `15a83d5` | **PASS** | 63/63 | 166.78 µs | 13.59 µs | 8,984/s |
| #26 | 2026-10-28 19:21:13 | `8671beca6667` | `e9b5795` | **PASS** | 63/63 | 364.94 µs | 14.20 µs | 8,415/s |
| #25 | 2026-10-28 18:50:14 | `8671beca6667` | `d740139` | **PASS** | 63/63 | 146.97 µs | 14.12 µs | 8,990/s |
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

