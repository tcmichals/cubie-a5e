# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-11-08 11:22:16
- **Target**: 192.168.3.3 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E906 RISC-V

## In-Kernel KUnit Driver Test Verification (60 Tests)

- **Overall Status**: **PASS** (60/60 tests passed, 0 failed)

| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| sunxi_rproc (Remoteproc Driver) | `sunxi_rproc` | 23 | 23 | 0 | 0 | **PASS** |
| sun55i_msgbox (Mailbox Driver) | `sun55i_msgbox` | 37 | 37 | 0 | 0 | **PASS** |
| **Combined Total** | **All In-Kernel Drivers** | **60** | **60** | **0** | **0** | **PASS** |

<details>
<summary>Click to view individual test case breakdown</summary>

### sunxi_rproc (Remoteproc Driver) (0 tests)

- *Summary validated: 23/23 passed via debugfs*

### sun55i_msgbox (Mailbox Driver) (0 tests)

- *Summary validated: 37/37 passed via debugfs*

</details>

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | KUnit: sunxi_rproc (Remoteproc Driver) (23/23 tests) | **PASS** |
| Profile 1 | KUnit: sun55i_msgbox (Mailbox Driver) (37/37 tests) | **PASS** |
| Profile 1 | run_tests.py (Complete Suite) | **PASS** |
| Profile 1 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 1 | C++ stress_rpmsg (100k, 16 in flight) | **PASS** |
| Profile 1 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 1 | C++ ping_dram (1000 pkts) | **PASS** |
| Profile 1 | C++ ping_dram (100k, 16 in flight) | **PASS** |
| Profile 1 | Python monitor_trace.py | **PASS** |
| Profile 1 | testFreeRTOS boot (tick + IRQ wake) | **PASS** |
| Profile 1 | FreeRTOS ping_rpmsg (1000 pkts) | **PASS** |
| Profile 1 | FreeRTOS stress_rpmsg (100k, 16 in flight) | **PASS** |
| Profile 1 | testExtraRam: dsp-ram (128 KB) | **PASS** |
| Profile 1 | testExtraRam: dram (1024 KB) | **PASS** |
| Profile 1 | testExtraRam: sram-a3-2 (512 KB) | **PASS** |
| Profile 1 | Msgbox clocks/resets (unbind -> bind) | **PASS** |
| Profile 1 | Msgbox ISR: lines held (3003000.mailbox, 3003000.mailbox-rv) | **PASS** |
| Profile 1 | Msgbox ISR: ping_rpmsg 10k, -D 0 | **PASS** |
| Profile 1 | Msgbox ISR: stress_rpmsg 100k, 16 in flight | **PASS** |
| Profile 1 | Msgbox ISR: FreeRTOS ping_rpmsg 2k (ISR wake) | **PASS** |
| Profile 1 | Msgbox ISR: write IRQ on SPI 205 (full -> pop -> 1 IRQ) | **PASS** |
| Profile 1 | Kernel Health (dmesg clean) | **PASS** |
| Profile 2 | Boot + remoteproc0 present | **PASS** |
| Profile 2 | run_tests.py (SRAM VirtIO) | **PASS** |
| Profile 2 | C++ ping_rpmsg (1000 pkts) | **PASS** |
| Profile 2 | C++ stress_rpmsg (100k, 16 in flight) | **PASS** |
| Profile 2 | Python ping_rpmsg.py (1000 pkts) | **PASS** |
| Profile 2 | Kernel Health (dmesg clean) | **PASS** |
| Profile 3 | Boot + remoteproc0 present | **PASS** |
| Profile 3 | run_tests.py (UIO Suite) | **PASS** |
| Profile 3 | C++ ping_shm (1000 pkts) | **PASS** |
| Profile 3 | C++ ping_uio (1000 pkts, IRQ) | **PASS** |
| Profile 3 | Python ping_uio.py (1000 pkts) | **PASS** |
| Profile 3 | Kernel Health (dmesg clean) | **PASS** |
| History | Metrics in range of last 10 PASS runs (rt+lockdep) | **PASS** |
| Source | RemoteProc VirtIO Doorbell Contract | **PASS** |
| Source | Mailbox TX Completion | **PASS** |
| Source | Zero-Sleep Benchmark Protocol | **PASS** |
| Source | PREEMPT_RT Hardirq Execution Bound | **PASS** |

## Message Box TX Completion

TX done by write IRQ: E906 bank write IRQ on SPI 205, armed after every send. RX/msg = SPI 0 interrupts per message.

| Benchmark | Mode | Status | Avg (µs) | Max (µs) | Jitter (µs) | p99 (µs) | msgs/s | RX IRQ/msg | TX IRQs | Queue overflows |
| :--- | :--- | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ping_rpmsg 10k, -D 0 | isr | **PASS** | 608.4 | 4,176.4 | 70.8 | 681.7 | 1,638.9 | 1.0 | 0 | 0 |
| stress_rpmsg 100k, 16 in flight | isr | **PASS** | 3,782.1 | 10,785.4 | - | 4,185.5 | 4,133.3 | 1.0 | 0 | 0 |
| FreeRTOS ping_rpmsg 2k (ISR wake) | isr | **PASS** | 661.3 | 858.2 | 32.3 | 764.5 | 1,509.1 | 1.0 | 2000 | 0 |


## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x30000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 623.04 µs | 1,601.0 msgs/s | 1.51 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ VirtIO, 16 in flight) | **PASS** | 100000/100000 | 3981.10 µs | 3,927.9 msgs/s | 3.72 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | 514.37 µs | 1,595.2 msgs/s | 199.4 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 193.23 µs | 3,307.5 msgs/s | 3.23 MB/sec | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR, 16 in flight) | **PASS** | 100000/100000 | 3014.82 µs | 5,117.6 msgs/s | 5.00 MB/sec | PASS (0 errors) |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 585.36 µs | 1,703.0 msgs/s | 1.61 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ SRAM VirtIO, 16 in flight) | **PASS** | 100000/100000 | 3716.47 µs | 4,202.8 msgs/s | 3.98 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | 498.13 µs | 1,637.4 msgs/s | 204.7 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Message box bound to sun55i-msgbox-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.86 µs | 8,496.7 msgs/s | 8.30 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 48.69 µs | 6,477.1 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 86.61 µs | 151.0 msgs/s | None | PASS (0 errors) |

