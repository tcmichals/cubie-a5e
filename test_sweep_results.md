# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-10-29 19:20:54
- **Target**: 192.168.3.3 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E906 RISC-V

## In-Kernel KUnit Driver Test Verification (63 Tests)

- **Overall Status**: **PASS** (63/63 tests passed, 0 failed)

| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| sunxi_rproc (Remoteproc Driver) | `sunxi_rproc` | 26 | 26 | 0 | 0 | **PASS** |
| sun55i_msgbox (Mailbox Driver) | `sun55i_msgbox` | 37 | 37 | 0 | 0 | **PASS** |
| **Combined Total** | **All In-Kernel Drivers** | **63** | **63** | **0** | **0** | **PASS** |

<details>
<summary>Click to view individual test case breakdown</summary>

### sunxi_rproc (Remoteproc Driver) (0 tests)

- *Summary validated: 26/26 passed via debugfs*

### sun55i_msgbox (Mailbox Driver) (0 tests)

- *Summary validated: 37/37 passed via debugfs*

</details>

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | KUnit: sunxi_rproc (Remoteproc Driver) (26/26 tests) | **PASS** |
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
| Profile 1 | Msgbox timer: lines held (3003000.mailbox) | **PASS** |
| Profile 1 | Msgbox timer: ping_rpmsg 10k, -D 0 | **PASS** |
| Profile 1 | Msgbox timer: stress_rpmsg 100k, 16 in flight | **PASS** |
| Profile 1 | Msgbox timer: FreeRTOS ping_rpmsg 2k (ISR wake) | **PASS** |
| Profile 1 | Kernel Health (dmesg clean) | **PASS** |
| Profile 1T | Boot + remoteproc0 present | **PASS** |
| Profile 1T | Msgbox ISR: lines held (3003000.mailbox, 3003000.mailbox-rv) | **PASS** |
| Profile 1T | Msgbox ISR: ping_rpmsg 10k, -D 0 | **PASS** |
| Profile 1T | Msgbox ISR: stress_rpmsg 100k, 16 in flight | **PASS** |
| Profile 1T | Msgbox ISR: FreeRTOS ping_rpmsg 2k (ISR wake) | **PASS** |
| Profile 1T | Msgbox ISR: write IRQ on SPI 205 (full -> pop -> 1 IRQ) | **PASS** |
| Profile 1T | Kernel Health (dmesg clean) | **PASS** |
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
| History | Metrics in range of last 10 PASS runs (rt) | **PASS** |
| Source | RemoteProc VirtIO Doorbell Contract | **PASS** |
| Source | Mailbox TX Completion | **PASS** |
| Source | Zero-Sleep Benchmark Protocol | **PASS** |
| Source | PREEMPT_RT Hardirq Execution Bound | **PASS** |

## Message Box TX Completion: Timer vs ISR

Timer: `last_tx_done()` = FIFO has room, 1 ms poll while full. ISR: overlay `cubie-a5e-msgbox-txirq` (`allwinner,tx-done-irq`), E906 bank write IRQ on SPI 205, enabled only while the FIFO is full. RX/msg = SPI 0 interrupts per message.

| Benchmark | Mode | Status | Avg (µs) | Max (µs) | Jitter (µs) | p99 (µs) | msgs/s | RX IRQ/msg | TX IRQs | Queue overflows |
| :--- | :--- | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ping_rpmsg 10k, -D 0 | timer | **PASS** | 166.3 | 233.9 | 9.1 | 195.2 | 5,988.9 | 1.0 | 0 | 0 |
| ping_rpmsg 10k, -D 0 | isr | **PASS** | 167.1 | 260.8 | 8.8 | 197.0 | 5,959.1 | 1.0 | 0 | 0 |
| stress_rpmsg 100k, 16 in flight | timer | **PASS** | 1,345.0 | 1,474.2 | - | 1,377.5 | 11,651.6 | 1.0 | 0 | 0 |
| stress_rpmsg 100k, 16 in flight | isr | **PASS** | 1,354.7 | 1,469.3 | - | 1,386.3 | 11,570.6 | 1.0 | 0 | 0 |
| FreeRTOS ping_rpmsg 2k (ISR wake) | timer | **PASS** | 188.7 | 250.5 | 8.1 | 216.5 | 5,278.7 | 1.0 | 0 | 0 |
| FreeRTOS ping_rpmsg 2k (ISR wake) | isr | **PASS** | 186.2 | 303.3 | 8.9 | 214.8 | 5,349.5 | 1.0 | 2000 | 0 |


## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x30000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 167.39 µs | 5,947.3 msgs/s | 5.63 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ VirtIO, 16 in flight) | **PASS** | 100000/100000 | 1345.03 µs | 11,651.5 msgs/s | 11.02 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | 120.77 µs | 6,067.5 msgs/s | 758.4 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 184.08 µs | 3,460.0 msgs/s | 3.38 MB/sec | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR, 16 in flight) | **PASS** | 100000/100000 | 2958.11 µs | 5,218.1 msgs/s | 5.10 MB/sec | PASS (0 errors) |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 131.25 µs | 7,571.1 msgs/s | 7.16 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ SRAM VirtIO, 16 in flight) | **PASS** | 100000/100000 | 762.74 µs | 20,396.8 msgs/s | 19.30 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | 105.30 µs | 6,706.7 msgs/s | 838.3 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Message box bound to sun55i-msgbox-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.57 µs | 8,792.7 msgs/s | 8.59 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 59.34 µs | 6,236.1 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 35.86 µs | 159.5 msgs/s | None | PASS (0 errors) |

