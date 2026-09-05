# Technical Articles & Engineering Publications

This directory contains long-form technical articles, multi-part engineering series, and case studies derived from the active bring-up of the Radxa Cubie A5E and Cubie A7A hardware platforms in the [cubie-a5e GitHub repository](https://github.com/tcmichals/cubie-a5e).

---

## 📚 Multi-Part Series: Bringing Up Heterogeneous RISC-V on Allwinner SoCs

A comprehensive 4-part series documenting the architecture, Linux driver development, verification, and bare-metal firmware design for the **XuanTie E907 RISC-V co-processor** on the **Allwinner T527 / A527 (`sun55i`)**:

1. **[Part 1: Architecture and Memory-Mapped Debugging](part1_heterogeneous_riscv_intro_architecture.md)**
   * **Topics**: Why use the co-processor (deterministic SRAM vs. DRAM, offloading Linux, safety), silicon taxonomy, T527 User Manual register mappings, ITCM/DTCM hardware architecture & direct RemoteProc mapping, and the JTAG-less DMEM paradigm.
2. **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**
   * **Topics**: Implementing `sunxi_rproc.c` on Linux 7.1, multi-segment `da_to_va` memory placement (ITCM, DTCM, PubSRAM C, Dedicated MCU SRAM), live trace logging via `.resource_table`, and systematically proving hardware state using the all-new `riscv-firmware/apps` verification suite (testBasic, testStringBinaryTrace0, testCrash, testPing, testPingRpmsg, testDRAMMsg).
3. **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
   * **Topics**: Memory determinism (zero-wait-state 1-cycle TCM vs DDR DRAM arbitration), lightweight lock-free circular ring buffer (libmetal) + Hardware Mailbox doorbell interrupts, interactive OpenOCD/GDB debugging, and introduction to stackless C++20 coroutines.
4. **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**
   * **Topics**: Deploying the open-source `AbstractX` framework on bare-metal RISC-V, triggering HALO (Heap Allocation eLision Optimization) for zero-cost coroutines, benchmarks vs FreeRTOS (19x speedup, <400 B RAM for 8 tasks), and non-blocking hardware awaiters.

---

## 📖 Case Studies & Deep Dives

* **[Mainline Flightstack Bring-Up Case Study](../buildroot/Mainline_Flightstack_Bringup_Article.md)**
  * **Topics**: Tri-domain real-time architecture across Linux PREEMPT_RT, XuanTie RISC-V, and FPGA domains.

* **[Open-Source NPU Migration Case Study](../buildroot/FOSS_NPU_Migration_Article.md)**
  * **Topics**: Migrating from proprietary vendor NPU binary blobs to upstream Linux `etnaviv` DRM driver and Mesa Teflon TFLite delegate.

* **[Mastering Dynamic Device Tree Overlays & UIO](devetreeOverlay.md)**
  * **Topics**: Multi-overlay boot chains in U-Boot (`boot.cmd`/`uEnv.txt`), converting Allwinner MSGBOX to generic UIO (`uio_pdrv_genirq`), dual MMIO mapping, and 0% CPU event-driven Python IPC with `select.epoll()`.

* **[Introduction to the Radxa Cubie A5E](introToCubieA5E.md)**
  * **Topics**: Comprehensive overview of the Radxa Cubie A5E SBC, Allwinner T527/A527 octa-core Cortex-A55 silicon, XuanTie E907 RISC-V co-processor, HiFi4 DSP, 2.0 TOPS VIP9000 NPU, and upstream mainline Linux ecosystem.
