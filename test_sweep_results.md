# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-10-03 00:04:23
- **Target**: 192.168.3.3 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E906 RISC-V

## In-Kernel KUnit Driver Test Verification (67 Tests)

- **Overall Status**: **PASS** (67/67 tests passed, 0 failed)

| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| sunxi_rproc (Remoteproc Driver) | `sunxi_rproc` | 33 | 33 | 0 | 0 | **PASS** |
| sun55i_msgbox (Mailbox Driver) | `sun55i_msgbox` | 34 | 34 | 0 | 0 | **PASS** |
| **Combined Total** | **All In-Kernel Drivers** | **67** | **67** | **0** | **0** | **PASS** |

<details>
<summary>Click to view individual test case breakdown</summary>

### sunxi_rproc (Remoteproc Driver) (0 tests)

- *Summary validated: 33/33 passed via debugfs*

### sun55i_msgbox (Mailbox Driver) (0 tests)

- *Summary validated: 34/34 passed via debugfs*

</details>

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | KUnit: sunxi_rproc (Remoteproc Driver) (33/33 tests) | **PASS** |
| Profile 1 | KUnit: sun55i_msgbox (Mailbox Driver) (34/34 tests) | **PASS** |
| Profile 1 | run_tests.py (Complete Suite) | **PASS** |
| Profile 1 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 1 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 1 | C++ ping_dram (1000 pkts) | **PASS** |
| Profile 1 | Python monitor_trace.py | **PASS** |
| Profile 1 | Kernel Health (dmesg clean) | **PASS** |
| Profile 2 | run_tests.py (SRAM VirtIO) | **PASS** |
| Profile 2 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 2 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 2 | Kernel Health (dmesg clean) | **PASS** |
| Profile 3 | run_tests.py (UIO Suite) | **PASS** |
| Profile 3 | C++ ping_shm (1000 pkts) | **PASS** |
| Profile 3 | Python ping_uio.py (1000 pkts) | **PASS** |
| Profile 3 | Kernel Health (dmesg clean) | **PASS** |

## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x30000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 517.75 µs | 1,507.3 msgs/s | 1.43 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | N/A | 7,478.5 msgs/s | 934.8 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 191.80 µs | 4,710.5 msgs/s | 4.60 MB/sec | Verified |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 136.43 µs | 7,291.4 msgs/s | 6.90 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | N/A | 6,313.5 msgs/s | 789.2 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Hardware Mailbox bound to generic-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.74 µs | 66,008.8 msgs/s | 64.46 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 13.81 µs | 63,407.4 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 178.49 µs | 4,559.1 msgs/s | None | PASS (0 errors) |

