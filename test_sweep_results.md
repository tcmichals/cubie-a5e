# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-09-25 14:06:37
- **Target**: 192.168.1.33 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E907 RISC-V

## In-Kernel KUnit Driver Test Verification (67 Tests)

- **Overall Status**: **PASS** (68/67 tests passed, 0 failed)

| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| sunxi_rproc (Remoteproc Driver) | `sunxi_rproc` | 34 | 34 | 0 | 0 | **PASS** |
| sun55i_msgbox (Mailbox Driver) | `sun55i_msgbox` | 34 | 34 | 0 | 0 | **PASS** |
| **Combined Total** | **All In-Kernel Drivers** | **68** | **68** | **0** | **0** | **PASS** |

<details>
<summary>Click to view individual test case breakdown</summary>

### sunxi_rproc (Remoteproc Driver) (0 tests)

- *Summary validated: 34/34 passed via debugfs*

### sun55i_msgbox (Mailbox Driver) (0 tests)

- *Summary validated: 34/34 passed via debugfs*

</details>

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | KUnit: sunxi_rproc (Remoteproc Driver) (34/34 tests) | **PASS** |
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
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 191.45 µs | 2,996.8 msgs/s | 2.84 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | N/A | 5,710.5 msgs/s | 713.8 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 202.10 µs | 4,499.0 msgs/s | 4.39 MB/sec | Verified |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 135.62 µs | 7,338.0 msgs/s | 6.94 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | N/A | 6,400.0 msgs/s | 800.0 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Hardware Mailbox bound to generic-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.72 µs | 66,317.4 msgs/s | 64.76 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 13.91 µs | 63,215.3 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 180.49 µs | 4,467.5 msgs/s | None | PASS (0 errors) |

