# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-09-21 22:07:15
- **Target**: 192.168.3.3 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E907 RISC-V

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | Kernel KUnit Tests (msgbox & rproc) | **PASS** |
| Profile 1 | run_tests.py (Complete Suite) | **FAIL** |
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
| ping_rpmsg (C++ VirtIO) | **FAIL** | 768/1000 | 175.64 µs | 4,816.2 msgs/s | 4.56 MB/s | FAIL |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | N/A | 6,068.0 msgs/s | 758.5 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 195.00 µs | 4,644.2 msgs/s | 4.54 MB/sec | Verified |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 125.97 µs | 7,893.9 msgs/s | 7.47 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | N/A | 7,084.4 msgs/s | 885.6 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Hardware Mailbox bound to generic-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.83 µs | 65,266.9 msgs/s | 63.74 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 13.96 µs | 62,697.7 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 179.65 µs | 4,539.9 msgs/s | None | PASS (0 errors) |

