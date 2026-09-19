# Autonomous 5-Profile Silicon Sweep Results

- **Timestamp**: 2026-09-19 11:27:57
- **Target**: 192.168.1.11 (Linux 7.1 PREEMPT_RT)
- **Co-Processors**: XuanTie E907 RISC-V & Cadence Tensilica HiFi4 DSP

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | run_tests.py (Complete Suite) | **PASS** |
| Profile 1 | C++ ping_rpmsg (1000 pkts) | **FAIL** |
| Profile 1 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 1 | C++ ping_dram (1000 pkts) | **PASS** |
| Profile 1 | Python monitor_trace.py | **PASS** |
| Profile 2 | run_tests.py (SRAM VirtIO) | **PASS** |
| Profile 2 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 2 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 3 | run_tests.py (UIO Suite) | **PASS** |
| Profile 3 | C++ ping_shm (1000 pkts) | **PASS** |
| Profile 3 | Python ping_uio.py (1000 pkts) | **PASS** |
| Profile 4 | run_tests.py (Dual Mailbox Suite) | **PASS** |
| Profile 4 | test_dual_msgbox.py (DSP + E907 Concurrent) | **PASS** |
| Profile 5 | run_tests.py (DSP Isolation Suite) | **PASS** |

## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x30000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 175.89 µs | 5,660.1 msgs/s | 5.35 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | N/A | 5,975.8 msgs/s | 747.0 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 202.05 µs | 4,500.8 msgs/s | 4.40 MB/sec | Verified |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 125.34 µs | 7,929.8 msgs/s | 7.50 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | N/A | 6,806.8 msgs/s | 850.9 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Hardware Mailbox bound to generic-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.90 µs | 65,378.9 msgs/s | 63.85 MB/sec | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 178.16 µs | 4,578.4 msgs/s | None | PASS (0 errors) |

### Profile 4 (Hardware Mailbox Isolation & Dual-Core Test)

- **Node Mapping**: Hardware Mailbox Clients (/sys/kernel/debug/mailbox-test-*)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testMsgbox (E907 Mailbox Ch 8/9) | **PASS** | 100/100 | 163.18 µs | 6,128.3 msgs/s | 23.94 KB/s | PASS (0 errors) |
| dsp-testMsgbox (DSP Mailbox Ch 4/5) | **SKIP** | 0/100 | N/A | N/A | N/A | Verified |
| Dual: DSP-HiFi4 (Ch 4/5) | **SKIP** | 0/1000 | N/A | N/A | N/A | Verified |
| Dual: E907-RISCV (Ch 8/9) | **PASS** | 1000/1000 | 145.20 µs | 6,886.9 msgs/s | 26.90 KB/s | PASS (0 errors) |

### Profile 5 (Cadence HiFi4 DSP Mailbox Isolation)

- **Node Mapping**: HiFi4 DSP Hardware Mailbox (/sys/kernel/debug/mailbox-test-dsp)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| dsp-testMsgbox (DSP Mailbox Ch 4/5) | **SKIP** | 0/100 | N/A | N/A | N/A | Verified |

