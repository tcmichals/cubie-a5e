# Technical Articles & Engineering Publications

This directory contains long-form technical articles, multi-part engineering series, and case studies derived from the active bring-up of the Radxa Cubie A5E and Cubie A7A hardware platforms in the [cubie-a5e GitHub repository](https://github.com/tcmichals/cubie-a5e).

---

## 📚 Multi-Part Series: Bringing Up Heterogeneous RISC-V on Allwinner SoCs

A comprehensive 4-part series documenting the architecture, Linux driver development, verification, and bare-metal firmware design for the **XuanTie E906 RISC-V co-processor** on the **Allwinner T527 / A527 (`sun55i`)**:

1. **[Part 1: Architecture and Memory-Mapped Debugging](part1_heterogeneous_riscv_intro_architecture.md)**
   * **Topics**: Why use the co-processor (deterministic SRAM vs. DRAM, offloading Linux, safety), silicon taxonomy, T527 User Manual register mappings, SRAM / TCM hardware architecture & RemoteProc boot mechanics, and the JTAG-less DMEM paradigm.
2. **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**
   * **Topics**: Implementing `sunxi_rproc.c` on Linux 7.1, memory-mapped ELF loading into PubSRAM C and Dedicated MCU SRAM, live trace logging via `.resource_table`, and systematically proving hardware state using the all-new `riscv-firmware/apps` verification suite (testBasic, testStringBinaryTrace0, testCrash, testPing, testPingRpmsg, testDRAMMsg).
3. **[Part 3: Ultra-Low-Latency IPC: Connecting Linux and RISC-V on Allwinner T527](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
   * **Topics**: Memory determinism (zero-wait-state 1-cycle TCM vs DDR DRAM arbitration), lightweight lock-free circular ring buffer (libmetal) + Hardware Mailbox doorbell interrupts, interactive OpenOCD/GDB debugging, and introduction to stackless C++20 coroutines.
4. **[Part 4: C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V: 19x Faster Context Switching](part4_deep_dive_baremetal_cpp_coroutines.md)**
   * **Topics**: Deploying the open-source `AbstractX` framework on bare-metal RISC-V, triggering HALO (Heap Allocation eLision Optimization) for zero-cost coroutines, benchmarks vs FreeRTOS (19x speedup, <400 B RAM for 8 tasks), and non-blocking hardware awaiters.
5. **[Part 5: Experiment First, Compile Later: Python on Both Processors of the Allwinner T527](https://github.com/tcmichals/AbstractX/blob/main/docs/articles/micropython-on-e906/README.md)** (in the AbstractX repository)
   * **Topics**: How the device-tree overlay and U-Boot hand UART3, I²C1 and DMA channels to the E906; remoteproc; RPMsg explained layer by layer (doorbell, vrings, addressed messages, endpoints, `/dev/rpmsgN`) and the three streams built on it; MicroPython running inside AbstractX on the E906; Python on Linux driving Python on the E906; live CPU, profiler and timeline views; sharing the DMA controller with Linux; board measurements.
6. **[Part 6: Heterogeneous RISC-V RemoteProc Across Allwinner Silicon: Cubie A7A (E902) vs. Cubie A5E (E906)](part5_a733_e902_remoteproc_and_heterogeneous_comparison.md)**
   * **Topics**: Bringing up Linux RemoteProc on Allwinner A733 (Radxa Cubie A7A) with XuanTie E902 (RV32EMC); comprehensive comparison with Allwinner T527 (Cubie A5E) XuanTie E906; memory models (SRAM A3 + HiFi4 DSP RAM vs System SRAM A2); D-cache coherency hazards in shared SRAM; apples-to-apples RPMsg benchmarks; and unified multi-core firmware architecture (`portASM_e90x.S`).

---

## 📖 Case Studies & Deep Dives

* **[Mainline Flightstack Bring-Up Case Study](../buildroot/Mainline_Flightstack_Bringup_Article.md)**
  * **Topics**: Tri-domain real-time architecture across Linux PREEMPT_RT, XuanTie RISC-V, and FPGA domains.

* **[Open-Source NPU Migration Case Study](../buildroot/FOSS_NPU_Migration_Article.md)**
  * **Topics**: Migrating from proprietary vendor NPU binary blobs to upstream Linux `etnaviv` DRM driver and Mesa Teflon TFLite delegate.

* **[Mastering Dynamic Device Tree Overlays & UIO](devicetreeOverlay.md)**
  * **Topics**: Multi-overlay boot chains in U-Boot (`boot.cmd`/`uEnv.txt`), converting Allwinner MSGBOX to generic UIO (`uio_pdrv_genirq`), dual MMIO mapping, and 0% CPU event-driven Python IPC with `select.epoll()`.

* **[Introduction to the Radxa Cubie A5E](introToCubieA5E.md)**
  * **Topics**: Comprehensive overview of the Radxa Cubie A5E SBC, Allwinner T527/A527 octa-core Cortex-A55 silicon, XuanTie E906 RISC-V co-processor, HiFi4 DSP, 2.0 TOPS VIP9000 NPU, and upstream mainline Linux ecosystem.
