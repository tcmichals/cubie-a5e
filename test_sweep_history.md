# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 5
- **Latest Run**: Run #5 (2026-10-14 18:12:06)
- **Kernel Commit**: `64c3c511452a` | **Firmware/Tools Commit**: `e676b78`
- **Latest Overall Status**: **FAIL**

### ⚠️ Regressions Detected Against Previous Run

- 🔴 **KUnit In-Kernel Tests (Passed): dropped from 67 to 47**

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Trend / Status |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 67 tests | 47 tests | `-20 tests` | 🔴 REGRESSION (Tests Failed!) |
| Profile 1 C++ Rpmsg RTT | 517.75 µs | 1242.68 µs | `+724.93 µs (+140.0%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 596.99 µs | 618.51 µs | `+21.52 µs (+3.6%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 1507.30 msgs/s | 719.00 msgs/s | `-788.30 msgs/s (-52.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 2 C++ Rpmsg RTT | 136.43 µs | 132.37 µs | `-4.06 µs (-3.0%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.74 µs | N/A | `N/A` | ⚪ Baseline |
| Profile 3 C++ UIO Throughput | 66008.80 msgs/s | N/A | `N/A` | ⚪ Baseline |
| Profile 3 C++ UIO Bandwidth | 64.46 MB/s | N/A | `N/A` | ⚪ Baseline |
| Profile 3 Python UIO RTT | 178.49 µs | N/A | `N/A` | ⚪ Baseline |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #5 | 2026-10-14 18:12:06 | `64c3c511452a` | `e676b78` | **FAIL** | 47/67 | 1242.68 µs | N/A | N/A |
| #4 | 2026-10-03 00:04:23 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 517.75 µs | 13.74 µs | 66,009/s |
| #3 | 2026-10-02 23:51:54 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 189.88 µs | 13.70 µs | 66,305/s |
| #2 | 2026-10-02 23:46:04 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 1242.43 µs | 13.75 µs | 66,138/s |
| #1 | 2026-10-02 23:38:50 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 200.16 µs | 13.85 µs | 65,244/s |

