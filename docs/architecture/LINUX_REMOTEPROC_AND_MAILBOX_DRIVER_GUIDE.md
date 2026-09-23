# Linux RemoteProc & Mailbox Driver Architectural Guide

**Author**: Tim Michals <tcmichals@gmail.com> / Embedded Real-Time Team  
**Scope**: Allwinner A523/A527/T527 (XuanTie E907), Allwinner A733 (XuanTie E902), and Mainline Linux Subsystems  
**Target Drivers**: `drivers/remoteproc/sunxi_rproc.c`, `drivers/mailbox/sun55i-msgbox.c`

---

## 1. Architectural Purpose & Overview

The Linux `remoteproc` (Remote Processor) and `mailbox` (Inter-Processor Communication) subsystems provide standard kernel frameworks to control heterogeneous co-processors (e.g., RISC-V, ARM Cortex-M/R, DSPs) and exchange doorbells or messages with them.

When developing production-grade, upstream-quality drivers for modern SoCs like the Allwinner A523/T527 or A733, drivers must adhere to strict kernel invariants regarding:
1. **Device Lifecycle**: Race-free probe, prepare, start, stop, unprepare, crash recovery, and remove sequences.
2. **Clock & Reset Domains**: Proper segregation between bus/interconnect resets and CPU pipeline resets.
3. **Memory Translation (`da_to_va`)**: Accurate mapping between co-processor local Device Addresses (DA), SoC Physical Addresses (PA), and Kernel Virtual Addresses (VA).
4. **SMP & Concurrency**: Preventing race conditions between workqueues, interrupts, mailbox callbacks, and device teardown.
5. **Rigorous KUnit Testing**: Testing actual driver logic and boundary conditions with mock MMIO registers without code duplication.

---

## 2. Mainline Comparison Matrix

How `sunxi_rproc` and `sun55i-msgbox` compare against key reference drivers in the upstream Linux kernel:

| Aspect | `sunxi_rproc` (Allwinner E907) | `imx_rproc` (NXP i.MX M4/M7) | `ti_k3_r5_remoteproc` (TI K3 R5F) | `stm32_rproc` (ST STM32MP1 M4) |
|---|---|---|---|---|
| **SoC Domain** | XuanTie E907 (RV32IMAFDC) | Cortex-M4/M7 microcontroller | Cortex-R5F in split/lockstep | Cortex-M4 microcontroller |
| **Reset Hierarchy** | Two-stage: `rst_cfg`/`rst_sram` (bus) vs `rst_core` (CPU) | SMC call or SRC registers | Two-stage: `module-reset` (bus/RAM) vs `local-reset` (CPU) | Syscon hold_boot / SCMI / SMC |
| **`.prepare()`** | Deasserts bus resets, enables clocks, enables SRAM remap, clears SRAM (`memset_io`) | Maps memory (`imx_rproc_addr_init`), enables clocks | Deasserts module-reset to allow loading internal RAM while CPU reset is held | Registers reserved memory carveouts |
| **`.start()`** | Deasserts `rst_core`, programs `STA_ADD_REG` boot address | Releases remote M4/M7 from reset | Releases local reset (`k3_rproc_release`) | Clears deep sleep (`pdds`), releases hold boot |
| **`.stop()`** | Asserts `rst_core`, then syncs `vq_work` | Asserts reset via SMC/MMIO, syncs workqueue | Asserts local reset (`k3_rproc_reset`) | Sends "detach" mbox msg, asserts hold boot |
| **`.unprepare()`** | Restores SRAM remap bit, disables CCU clocks, asserts bus resets | Disables clocks | Asserts module-reset via TI-SCI | N/A |
| **`.da_to_va()`** | Translates Space 0 (Host PA, DA 0x3ff80000, 0x3ffc0000, 0x00020000), Space 1 (PA, DA 0x40000000, 0x40040000); returns `NULL` for DDR carveouts | Static table lookup (`imx_rproc_att`) across TCML, TCMU, DDR | Iterates `mem[]` (internal RAM) and `rmem[]` (DDR); returns `cpu_addr + offset` | Dynamic lookup in `rmems` based on `dma-ranges` |
| **`.kick()`** | Sends `vqid` via `priv->kick_msg` struct member (avoids stack UAF), calls `mbox_client_txdone()` | Iterates `rproc->notifyids` in workqueue | Casts `msg` to `(void *)(uintptr_t)` and sends via mbox | Dedicated mailbox channels per virtqueue |
| **Crash Handling** | `disable_irq_nosync()` + `rproc_report_crash(rproc, RPROC_FATAL_ERROR)`; re-enabled on `.start()` | N/A | N/A | Dedicated watchdog IRQ -> `rproc_report_crash(rproc, RPROC_WATCHDOG)` |

---

## 3. The Ten Rules of Linux RemoteProc & Mailbox Drivers

### Rule 1: Segregate Bus Resets from CPU Pipeline Resets
- **The Problem**: If an ARM host attempts to write firmware segments into remote co-processor SRAM or configure registers while the interconnect/bus is held in reset, a synchronous external abort (bus fault) occurs. Conversely, if the CPU pipeline is taken out of reset before firmware loading completes, the core executes random uninitialized memory.
- **The Solution**: 
  - In `.prepare()`: Deassert bus/interconnect resets (`rst_cfg`, `rst_sram`, `rst_msgbox`) and enable clocks. Leave `rst_core` asserted.
  - In `.start()`: Deassert `rst_core` only after firmware loading completes and after programming the entry point register (`STA_ADD_REG`).

### Rule 2: Initialize Workqueues Before Requesting Interrupts or Mailbox Channels
- **The Problem**: If `mbox_request_channel_byname()` is called before `INIT_WORK(&priv->vq_work, ...)`, any incoming message from a previously running remote core will trigger `rx_callback`, which calls `schedule_work()` on an uninitialized `work_struct`, crashing the kernel. Furthermore, if probe fails with `-EPROBE_DEFER`, cleanup code calling `cancel_work_sync()` on an uninitialized work struct will trigger a kernel BUG.
- **The Invariant**: `INIT_WORK()` must precede any call to request mailbox channels or register interrupt handlers.

### Rule 3: Stop Interrupt Sources Before Draining Workqueues on Teardown
- **The Problem**: If `cancel_work_sync(&priv->vq_work)` is called *before* `rproc_del()`, `rproc_del()` shuts down the co-processor and triggers virtio teardown. During this window, a late hardware interrupt can re-queue `vq_work` after `cancel_work_sync()` returns, causing the work handler to execute after resources (like mailbox channels) have been freed.
- **The Invariant**:
  ```c
  /* Correct LIFO Teardown Order */
  if (priv->crash_irq > 0)
      disable_irq(priv->crash_irq);  /* 1. Silence crash IRQ */
  rproc_del(rproc);                  /* 2. Stop core, shutdown virtio */
  cancel_work_sync(&priv->vq_work);  /* 3. Drain any pending work */
  if (priv->rx_chan)
      mbox_free_channel(priv->rx_chan); /* 4. Free channels */
  ```

### Rule 4: Synchronize Hardirqs Before Cutting Clocks or Resets
- **The Problem**: In SMP systems, if driver teardown (`remove()`) masks IRQs in hardware, asserts reset, and disables clocks while the ISR is actively running on another CPU core, that core performs MMIO reads on an unclocked bus, causing a hardware bus fault.
- **The Solution**: Call `synchronize_irq(irq)` on all requested interrupts before asserting reset lines or disabling peripheral clocks.

### Rule 5: Mitigate Crash Interrupt Storms
- **The Problem**: Level-sensitive hardware crash alerts or watchdogs remain asserted until the remote core is completely reset. If a driver simply calls `rproc_report_crash()` in the hardirq handler, the interrupt immediately re-fires millions of times per second (IRQ storm), freezing the ARM Linux host.
- **The Solution**:
  ```c
  static irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
  {
      struct rproc *rproc = data;
      struct sunxi_rproc *priv = rproc->priv;

      dev_err(priv->dev, "Crash event from remote core!\n");
      disable_irq_nosync(irq); /* Silence storm immediately */
      rproc_report_crash(rproc, RPROC_FATAL_ERROR);
      return IRQ_HANDLED;
  }
  ```
  The interrupt is safely re-enabled in `sunxi_rproc_start()` when the core is brought back to life during recovery.

### Rule 6: Never Pass Stack Pointers to Asynchronous Mailbox Send
- **The Problem**: When a mailbox client operates with `tx_block = false`, `mbox_send_message()` returns immediately while the mailbox driver queues or processes the pointer. Passing `&local_var` on the stack results in a stack Use-After-Return (UAF).
- **The Solution**: Store the kick payload in persistent driver context (e.g., `priv->kick_msg = vqid; mbox_send_message(priv->tx_chan, &priv->kick_msg);`).

### Rule 7: Bound All FIFO Drain Loops in Hardirqs
- **The Problem**: A `while (readl(STATUS) & HAS_DATA)` loop in a hardirq handler will lock up the CPU if the remote hardware register fails or gets stuck in a FIFO-full condition.
- **The Invariant**: Cap all FIFO drain loops to the hardware FIFO depth (`for (i = 0; i < FIFO_MAX; i++)`).

### Rule 8: Guard Address Calculations Against Integer Overflow
- **The Problem**: In `da_to_va(rproc, da, len)`, if a crafted ELF firmware contains `da = 0xFFFFFFFFFFFFFFF0` and `len = 0x20`, `da + len` overflows `u64` to `0x10`. This passes upper-bound checks (`(da + len) <= window_end`), allowing arbitrary kernel memory to be mapped and overwritten during firmware loading.
- **The Invariant**:
  ```c
  if (len == 0 || da > U64_MAX - len)
      return NULL;
  ```

### Rule 9: Delegate Dynamic DDR Carveouts to the RemoteProc Core
- **The Question**: Why does `sunxi_rproc_da_to_va()` return `NULL` for DDR carveouts?
- **The Answer**: The Linux remoteproc core manages dynamic DMA carveouts via `rproc->carveouts`. When `sunxi_rproc_parse_memory_regions()` registers reserved-memory nodes using `rproc_add_carveout()`, the core automatically translates addresses that fall within these carveouts whenever `.da_to_va()` returns `NULL`. Driver-specific `.da_to_va()` should only handle SoC-specific on-chip SRAM/TCM windows that have unique core-local DA aliases.

### Rule 10: Unit Tests Must Directly Exercise Driver Functions (No Mirroring)
- **The Antipattern**: Writing a KUnit test that duplicates the driver's routing tables, address math, or macros inside the test file. Such tests are tautological—they test their own copy-pasted math rather than the driver code.
- **The Invariant**: Export internal driver functions under `#if IS_ENABLED(CONFIG_..._KUNIT_TEST)` and call them directly in the test suite using mock MMIO fixtures.

---

## 4. Platform Differences: A523/T527 (E907) vs. A733 (E902)

| Feature | Allwinner A523 / T527 (Cubie A5E) | Allwinner A733 (Cubie A7A) |
|---|---|---|
| **Co-Processor** | XuanTie E907 (RV32IMAFDC + FPU) | XuanTie E902 (RV32EMC, Integer only) |
| **Domain** | Dedicated Open MCU Domain (`0x07100000`) | CPUS / Always-On Domain (`0x07000000`) |
| **TrustZone Firewall** | Open Non-Secure access to CFG block | BL31 Secure World filtered by default |
| **Start Address Register** | `0x07130204` (`STA_ADD_REG`) | `0x07032204` (`E902_STA_ADD_REG`) |
| **Default Boot Target** | `0x40014000` (or SRAM `0x07280000`) | `0x40014000` (`scp.fex` DRAM offset) |
| **SRAM Architecture** | 256 KB Space 0 + 256 KB Space 1 | 208 KB SRAM A2 (`0x00040000`–`0x00073FFF`) |
| **Remap Control** | `SUNXI_REMAP_CTRL_OFFSET` bit 1 (SRAMA3_2) | CPUS SRAM remap syscon |

When adapting `sunxi_rproc.c` for A733 support:
1. Provide a distinct `sunxi_rproc_cfg` struct for A733 matching `allwinner,sun60i-a733-rproc`.
2. Handle the alternative CFG and STA_ADD register offsets (`0x07032204`).
3. Accommodate SRAM A2 mapping boundaries in `da_to_va`.

---

## 5. KUnit Testing Architecture

To achieve a >2:1 test-to-code ratio with comprehensive coverage, the test suites use **Mock MMIO Fixtures**:
- Rather than requiring real silicon registers or executing on target hardware, tests allocate structured mock arrays representing hardware register banks.
- Functional driver operations (`send_data`, `last_tx_done`, `peek_data`, `startup`, `shutdown`, `da_to_va`, `start`, `prepare`) are executed against these mock banks.
- Every valid route, boundary condition, integer overflow, and error path is systematically asserted.
