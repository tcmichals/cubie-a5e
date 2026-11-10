# Radxa Cubie A7A Silicon Sweep Results

- **Timestamp**: 2026-11-10 06:36:23
- **Target**: 192.168.1.12 (Linux 7.1 PREEMPT_RT)
- **Board**: Radxa Cubie A7A (Allwinner A733)
- **Co-Processor**: XuanTie E902 RISC-V (RV32EMC @ 200 MHz, 208 KB SRAM A2)
- **Overall Status**: **FAIL**

## Overall Test Summary

| Platform | Test / Tool | Status |
| :--- | :--- | :---: |
| A7A | KUnit: sunxi_rproc (Remoteproc Driver) (0/0 tests) | **MISSING** |
| A7A | KUnit: sun55i_msgbox (Mailbox Driver) (0/0 tests) | **MISSING** |
| A7A | run_tests.py (A7A Suite) | **FAIL** |
| A7A | C++ ping_rpmsg (1000 pkts) | **FAIL** |
| A7A | C++ stress_rpmsg (100k, 16 in flight) | **FAIL** |
| A7A | Python ping_rpmsg.py (1000 pkts) | **FAIL** |
| A7A | C++ ping_dram (1000 pkts) | **FAIL** |
| A7A | Python monitor_trace.py | **FAIL** |
| A7A | exampleRiscv lifecycle (start/running/stop) | **PASS** |
| A7A | Kernel Health (dmesg clean) | **FAIL** |
