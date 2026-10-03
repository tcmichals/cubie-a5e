# Silicon Sweep Performance History & Regression Tracking

- **Total Recorded Runs**: 4
- **Latest Run**: Run #4 (2026-10-03 00:04:23)
- **Kernel Commit**: `b861276e3e76` | **Firmware/Tools Commit**: `c347362`
- **Latest Overall Status**: **PASS**

### ⚠️ Regressions Detected Against Previous Run

- 🔴 **Profile 1 C++ Rpmsg Rate: 2996.30 -> 1507.30 msgs/s (-49.7%)**

## Comparison Against Preceding Run

| Metric | Previous Run | Current Run | Delta | Trend / Status |
| :--- | :---: | :---: | :---: | :---: |
| KUnit In-Kernel Tests (Passed) | 67 tests | 67 tests | `+0 tests` | 🟢 Invariant (100% Pass) |
| Profile 1 C++ Rpmsg RTT | 189.88 µs | 517.75 µs | `+327.88 µs (+172.7%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Jitter | 4.45 µs | 596.99 µs | `+592.54 µs (+13315.5%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 1 C++ Rpmsg Rate | 2996.30 msgs/s | 1507.30 msgs/s | `-1489.00 msgs/s (-49.7%)` | ⚠️ REGRESSION (Exceeds Noise Floor) |
| Profile 2 C++ Rpmsg RTT | 140.14 µs | 136.43 µs | `-3.71 µs (-2.6%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Direct SRAM RTT | 13.70 µs | 13.74 µs | `+0.04 µs (+0.3%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Throughput | 66304.90 msgs/s | 66008.80 msgs/s | `-296.10 msgs/s (-0.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 C++ UIO Bandwidth | 64.75 MB/s | 64.46 MB/s | `-0.29 MB/s (-0.4%)` | 🟢 Invariant (Within Normal Jitter Band) |
| Profile 3 Python UIO RTT | 178.89 µs | 178.49 µs | `-0.40 µs (-0.2%)` | 🟢 Invariant (Within Normal Jitter Band) |

## Historical Runs Timeline

| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #4 | 2026-10-03 00:04:23 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 517.75 µs | 13.74 µs | 66,009/s |
| #3 | 2026-10-02 23:51:54 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 189.88 µs | 13.70 µs | 66,305/s |
| #2 | 2026-10-02 23:46:04 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 1242.43 µs | 13.75 µs | 66,138/s |
| #1 | 2026-10-02 23:38:50 | `b861276e3e76` | `c347362` | **PASS** | 67/67 | 200.16 µs | 13.85 µs | 65,244/s |

