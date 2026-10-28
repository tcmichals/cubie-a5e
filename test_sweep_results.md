# Autonomous 3-Profile Silicon Sweep Results

- **Timestamp**: 2026-10-27 18:51:35
- **Target**: 192.168.3.3 (Linux 7.1 PREEMPT_RT)
- **Co-Processor**: XuanTie E906 RISC-V

## In-Kernel KUnit Driver Test Verification (63 Tests)

- **Overall Status**: **PASS** (61/63 tests passed, 0 failed)

| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| sunxi_rproc (Remoteproc Driver) | `sunxi_rproc` | 26 | 26 | 0 | 0 | **PASS** |
| sun55i_msgbox (Mailbox Driver) | `sun55i_msgbox` | 37 | 35 | 0 | 2 | **PASS** |
| **Combined Total** | **All In-Kernel Drivers** | **63** | **61** | **0** | **2** | **PASS** |

<details>
<summary>Click to view individual test case breakdown</summary>

### sunxi_rproc (Remoteproc Driver) (0 tests)

- *Summary validated: 26/26 passed via debugfs*

### sun55i_msgbox (Mailbox Driver) (0 tests)

- *Summary validated: 35/37 passed via debugfs*

</details>

## Overall Test Summary

| Profile | Test / Tool | Status |
| :--- | :--- | :---: |
| Profile 1 | KUnit: sunxi_rproc (Remoteproc Driver) (26/26 tests) | **PASS** |
| Profile 1 | KUnit: sun55i_msgbox (Mailbox Driver) (35/37 tests) | **PASS** |
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

## Message Box TX Completion: Timer vs ISR

Timer: `last_tx_done()` = FIFO has room, 1 ms poll while full. ISR: overlay `cubie-a5e-msgbox-txirq` (`allwinner,tx-done-irq`), E906 bank write IRQ on SPI 205, enabled only while the FIFO is full. RX/msg = SPI 0 interrupts per message.

| Benchmark | Mode | Status | Avg (µs) | Max (µs) | Jitter (µs) | p99 (µs) | msgs/s | RX IRQ/msg | TX IRQs | Queue overflows |
| :--- | :--- | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ping_rpmsg 10k, -D 0 | timer | **PASS** | 167.2 | 230.9 | 8.2 | 194.5 | 5,958.0 | 1.0 | 0 | 0 |
| ping_rpmsg 10k, -D 0 | isr | **PASS** | 167.3 | 235.0 | 8.1 | 195.9 | 5,953.4 | 1.0 | 0 | 0 |
| stress_rpmsg 100k, 16 in flight | timer | **PASS** | 1,361.2 | 1,617.4 | - | 1,393.0 | 11,513.9 | 1.0 | 0 | 0 |
| stress_rpmsg 100k, 16 in flight | isr | **PASS** | 1,373.4 | 1,532.2 | - | 1,405.2 | 11,415.3 | 1.0 | 0 | 0 |
| FreeRTOS ping_rpmsg 2k (ISR wake) | timer | **PASS** | 193.6 | 274.7 | 9.3 | 230.4 | 5,146.2 | 1.0 | 0 | 0 |
| FreeRTOS ping_rpmsg 2k (ISR wake) | isr | **PASS** | 191.0 | 265.4 | 9.6 | 223.6 | 5,214.4 | 1.0 | 2000 | 0 |


## Driver Line Coverage on the Board (gcov)

Counters saved before every reboot and combined: a line is covered if any profile ran it. UML KUnit covers the rest of the drivers; this run measures probe(), remove() and the hardware accessors.

| Driver | Lines covered | Percent |
| :--- | ---: | ---: |
| `drivers/mailbox/sun55i-msgbox.c` | 268/277 | 96.75% |
| `drivers/remoteproc/sunxi_rproc.c` | 340/379 | 89.71% |

<details>
<summary>drivers/mailbox/sun55i-msgbox.c: 9 lines not reached</summary>

| Line | Source |
| ---: | :--- |
| 491 | `return dev_err_probe(dev, PTR_ERR(mbox->regs[i]),` |
| 503 | `return dev_err_probe(dev, ret, "failed to enable clocks\n");` |
| 505 | `return dev_err_probe(dev, -EINVAL, "need %d clocks, found %d\n",` |
| 510 | `return dev_err_probe(dev, ret, "failed to count resets\n");` |
| 512 | `return dev_err_probe(dev, -EINVAL, "need %d resets, found %d\n",` |
| 517 | `return dev_err_probe(dev, PTR_ERR(rst), "failed to get resets\n");` |
| 527 | `return dev_err_probe(dev, ret, "failed to deassert reset\n");` |
| 556 | `return dev_err_probe(dev, ret, "failed to request irq\n");` |
| 591 | `return dev_err_probe(dev, ret, "failed to register controller\n");` |

</details>

<details>
<summary>drivers/remoteproc/sunxi_rproc.c: 39 lines not reached</summary>

| Line | Source |
| ---: | :--- |
| 220 | `dev_warn(priv->dev, "crash dump segments incomplete: %d\n", ret);` |
| 490 | `ret = -ENOMEM;` |
| 491 | `goto err_free_mem;` |
| 504 | `goto err_free_mem;` |
| 513 | `err_free_mem:` |
| 514 | `while (i--)` |
| 515 | `kfree(mem[i]);` |
| 547 | `return dev_err_probe(dev, PTR_ERR(*va), "failed to map '%s'\n", name);` |
| 551 | `return dev_err_probe(dev, -EINVAL, "'%s' %pR is not in the %s address map\n",` |
| 552 | `name, res, priv->cfg->name);` |
| 568 | `return dev_err_probe(dev, PTR_ERR(priv->cfg_va), "failed to map 'cfg'\n");` |
| 586 | `return dev_err_probe(dev, -EINVAL, "missing 'remap' register\n");` |
| 589 | `return dev_err_probe(dev, -ENOMEM, "failed to map 'remap'\n");` |
| 609 | `return dev_err_probe(dev, -EINVAL, "too many memory-region entries (%d)\n",` |
| 615 | `return dev_err_probe(dev, ret, "failed to set up the DMA pool\n");` |
| 628 | `return dev_err_probe(dev, ret, "invalid memory-region %d\n", i);` |
| 632 | `return dev_err_probe(dev, -EINVAL,` |
| 657 | `return dev_err_probe(dev, -EINVAL,` |
| 664 | `return dev_err_probe(dev, PTR_ERR(rmem->va),` |
| 700 | `return dev_err_probe(dev, ret, "invalid firmware-name\n");` |
| 709 | `return dev_err_probe(dev, ret, "failed to set 32-bit DMA coherent mask\n");` |
| 729 | `return dev_err_probe(dev, ret, "failed to attach power domains\n");` |
| 737 | `return dev_err_probe(dev, PTR_ERR(priv->clk_bus), "failed to get 'bus' clock\n");` |
| 741 | `return dev_err_probe(dev, PTR_ERR(priv->clk_core), "failed to get 'core' clock\n");` |
| 745 | `return dev_err_probe(dev, PTR_ERR(priv->clk_sram), "failed to get 'sram' clock\n");` |
| 749 | `return dev_err_probe(dev, PTR_ERR(priv->rst_cfg), "failed to get 'cfg' reset\n");` |
| 753 | `return dev_err_probe(dev, PTR_ERR(priv->rst_core), "failed to get 'core' reset\n");` |
| 757 | `return dev_err_probe(dev, PTR_ERR(priv->rst_sram), "failed to get 'sram' reset\n");` |
| 761 | `return dev_err_probe(dev, PTR_ERR(priv->rst_debug),` |
| 777 | `ret = devm_request_threaded_irq(dev, crash_irq, NULL,` |
| 782 | `if (ret)` |
| 783 | `return dev_err_probe(dev, ret, "failed to request crash IRQ\n");` |
| 784 | `priv->crash_irq = crash_irq;` |
| 786 | `return dev_err_probe(dev, crash_irq, "failed to get crash IRQ\n");` |
| 806 | `ret = PTR_ERR(priv->mbox_chan);` |
| 807 | `priv->mbox_chan = NULL;` |
| 808 | `return dev_err_probe(dev, ret, "failed to request mailbox\n");` |
| 816 | `sunxi_rproc_free_mbox(priv);` |
| 817 | `return dev_err_probe(dev, ret, "failed to register rproc device\n");` |

</details>


## Detailed Quantitative Metrics per Profile

### Profile 1 (Standard DDR VirtIO & Carveout)

- **Node Mapping**: DDR DRAM Carveout / vdev@48000000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| testCrash.elf (Autopsy) | **PASS** | N/A | 0x30000002 | N/A | N/A | Verified |
| ping_rpmsg (C++ VirtIO) | **PASS** | 1000/1000 | 167.52 µs | 5,944.8 msgs/s | 5.62 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ VirtIO, 16 in flight) | **PASS** | 100000/100000 | 1361.56 µs | 11,514.1 msgs/s | 10.89 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python VirtIO) | **PASS** | 1000/1000 | 121.45 µs | 6,145.4 msgs/s | 768.2 KB/s | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR) | **PASS** | 1000/1000 | 195.03 µs | 3,332.1 msgs/s | 3.25 MB/sec | PASS (0 errors) |
| ping_dram (C++ Hybrid DDR, 16 in flight) | **PASS** | 100000/100000 | 3044.72 µs | 5,077.5 msgs/s | 4.96 MB/sec | PASS (0 errors) |

### Profile 2 (Pure On-Chip SRAM VirtIO)

- **Node Mapping**: On-Chip SRAM Space 1 / sram1@72c0000

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| testBasic.elf | **PASS** | 1 beats | N/A | N/A | N/A | Verified |
| testStringBinaryTrace0.elf | **PASS** | N/A | N/A | N/A | N/A | Verified |
| ping_rpmsg (C++ SRAM VirtIO) | **PASS** | 1000/1000 | 131.31 µs | 7,574.6 msgs/s | 7.17 MB/s | PASS (0 errors) |
| stress_rpmsg (C++ SRAM VirtIO, 16 in flight) | **PASS** | 100000/100000 | 760.64 µs | 20,450.2 msgs/s | 19.35 MB/s | PASS (0 errors) |
| ping_rpmsg.py (Python SRAM VirtIO) | **PASS** | 1000/1000 | 102.60 µs | 7,061.3 msgs/s | 882.7 KB/s | PASS (0 errors) |

### Profile 3 (Userspace UIO Direct Mailbox)

- **Node Mapping**: Message box bound to sun55i-msgbox-uio (/dev/uio0)

| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ping_shm (C++ Direct SRAM) | **PASS** | 1000/1000 | 13.73 µs | 8,794.9 msgs/s | 8.59 MB/sec | PASS (0 errors) |
| ping_uio (C++ UIO Doorbell) | **PASS** | 1000/1000 | 59.35 µs | 6,233.3 msgs/s | None | PASS (0 errors) |
| ping_uio.py (Python UIO Doorbell) | **PASS** | 1000/1000 | 35.53 µs | 159.9 msgs/s | None | PASS (0 errors) |

