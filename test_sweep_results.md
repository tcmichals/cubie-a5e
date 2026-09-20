# Autonomous 4-Profile Silicon Sweep Results

- **Timestamp**: 2026-09-19 14:30:00
- **Target**: 192.168.1.19 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E907 RISC-V (`remoteproc0`)
- **Archive Reference**: Cadence HiFi4 DSP experiments archived at tag `v2.1.0-dsp-archive`

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | run_tests.py (Complete Suite) | **PASS** |
| Profile 1 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 1 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 1 | C++ ping_dram (1000 pkts) | **PASS** |
| Profile 1 | Python monitor_trace.py | **PASS** |
| Profile 2 | run_tests.py (SRAM VirtIO) | **PASS** |
| Profile 2 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 2 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 3 | run_tests.py (UIO Suite) | **PASS** |
| Profile 3 | C++ ping_shm (1000 pkts) | **PASS** |
| Profile 3 | Python ping_uio.py (1000 pkts) | **PASS** |
| Profile 4 | run_tests.py (Mailbox Suite) | **PASS** |

## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 4 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x00000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 172.16 µs | 5,784.6 msgs/s | 5.47 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | 126.54 µs | 6,166.4 msgs/s | 770.8 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 191.88 µs | 4,710.2 msgs/s | 4.60 MB/sec | Verified |

### Profile 2 (Pure On-Chip SRAM Space 1 VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 4 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 125.54 µs | 7,920.0 msgs/s | 7.49 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | 110.80 µs | 7,272.6 msgs/s | 909.1 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox & SRAM)

- **Node Mapping**: Hardware Mailbox bound to generic-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.84 µs | 65,770.7 msgs/s | 64.23 MB/sec | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 180.34 µs | 4,459.6 msgs/s | 557.4 KB/s | PASS (0 errors) |

### Profile 4 (Hardware Mailbox Direct Loopback)

- **Node Mapping**: Hardware Mailbox Clients (/sys/kernel/debug/mailbox-test-e907)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testMsgbox (E907 Mailbox Ch 8/9) | **PASS** | 100/100 | 135.64 µs | 7,372.2 msgs/s | 28.80 KB/s | PASS (0 errors) |
