# RemoteProc, VirtIO RPMsg, and Mailbox Subsystem Deep Invariants

This guide documents the tripartite subsystem architecture connecting **RemoteProc (`drivers/remoteproc/`)**, **VirtIO RPMsg (`drivers/rpmsg/`)**, and **Mailbox (`drivers/mailbox/`)**. 

It details the core subsystem contracts, memory barriers, lifecycle state machines, and historical bug patterns across all three layers to ensure the **Allwinner sun55i (A523/A527/T527) XuanTie E906 RemoteProc and Message Box drivers** remain robust under stress, crash recovery, and module unbind.

---

## 1. RemoteProc Core Framework (`remoteproc_core.c`, `remoteproc_elf_loader.c`)

### A. Lifecycle State Machine & Crash Recovery
The RemoteProc framework manages the core state machine:
```text
[RPROC_OFFLINE] ──rproc_boot()──> [RPROC_RUNNING] ──rproc_shutdown()──> [RPROC_OFFLINE]
       │                                 │
       │                                 ├──rproc_report_crash()
       │                                 ▼
       └────────────────────────── [RPROC_CRASHED]
```

#### Key Invariant: Recovery Skips `.unprepare()`
1. In standard boot (`rproc_boot()`):
   - `.prepare()` $\rightarrow$ Load Segments $\rightarrow$ `.start()` $\rightarrow$ Probe Subdevices (VirtIO).
2. In standard shutdown (`rproc_shutdown()`):
   - Stop Subdevices $\rightarrow$ `.stop()` $\rightarrow$ `.unprepare()` $\rightarrow$ Clean Carveouts (`rproc_resource_cleanup()`).
3. **In Crash Recovery (`rproc_trigger_recovery()` / `b327c72753d6`)**:
   - The core stops subdevices and executes `.stop()`, but **does NOT call `.unprepare()`**!
   - It reloads firmware segments and directly calls `.start()`.
   - **Critical Rule**: Any resource required for core execution that was configured in `.prepare()` (such as power domains, clock gates, or SRAM remap bits) remains active during recovery. If a driver de-asserts or unmaps those resources in `.stop()`, the core will fail to boot during crash recovery!

---

### B. ELF Loading & `da_to_va()` Boundary Invariants
In `rproc_elf_load_segments()`:
- The loader translates every ELF program header `p_paddr` using `rproc_da_to_va(rproc, da, memsz, &is_iomem)`.
- If `p_memsz > p_filesz`, the loader automatically zeroes the uninitialized BSS section using `memset()`.
- **The Pitfall**: On ARM64, if `is_iomem` is `true` (MMIO/SRAM), performing standard unaligned 64-bit `memset()` can trigger hardware Bus Faults on device memory.
- **Sunxi Implementation**: Dedicated SRAM Space 0 and Space 1 are mapped via `devm_ioremap_resource_wc()` with `is_iomem = true`, and pre-cleared via `memset_io()` in `.prepare()` before ELF loading begins.

---

## 2. VirtIO RPMsg Subsystem (`virtio_rpmsg_bus.c`, `remoteproc_virtio.c`)

### A. Dual-VRing Topology & Buffer Geometry
RemoteProc VirtIO establishes two Virtqueues:
- **vring 0 (Host-to-Remote / TX)**: Host deposits outgoing messages.
- **vring 1 (Remote-to-Host / RX)**: Coprocessor deposits incoming messages.

```text
 ┌─────────────────────────────────────────────────────────────┐
 │ VirtIO RPMsg Shared DMA Pool (memory-region[0] / "vram")    │
 ├───────────────────────────────┬─────────────────────────────┤
 │ Vring 0 Descriptors & Buffers │ Vring 1 Descriptors & Buffers│
 │ (496-byte payload + 16B hdr)  │ (496-byte payload + 16B hdr)│
 └───────────────────────────────┴─────────────────────────────┘
```

#### Key Invariant: Buffer Allocation
- When firmware resource table declares `FW_RSC_ADDR_ANY` for vrings and buffers, the RemoteProc core allocates memory from the device's default DMA pool (`of_reserved_mem_device_init()`).
- In `sun55i-a523.dtsi`, `memory-region = <&rproc_vdev>, ...` specifies `&rproc_vdev` at index 0 as `compatible = "shared-dma-pool"`.
- This ensures zero dynamic CMA contention and prevents non-cacheable DMA conflicts.

---

### B. Memory Barrier & Doorbell Ordering
VirtIO ring buffers rely on memory barriers:
1. When the host sends data:
   - Data is copied into the RPMsg buffer.
   - `virtqueue_add_outbuf()` updates the vring available index.
   - `virtio_wmb()` commits the descriptor writes to RAM.
   - `rproc_virtio_notify()` $\rightarrow$ `ops->kick()` $\rightarrow$ triggers hardware mailbox doorbell.
2. **The Hardware Interconnect Hazard**:
   - On heterogeneous ARM64 SoCs, MMIO writes (`writel()`) to the mailbox controller may bypass non-cacheable write buffers under weak ordering.
   - **Rule**: A write barrier (`wmb()`) or `readl()` flush ensures preceding memory stores are visible to the coprocessor before the doorbell interrupt arrives at the remote core.

---

### C. Teardown Race: `rproc_del()` vs In-Flight Vring Work
- **The Mathieu Poirier Rule (`35bdafda40cc`)**:
  When `rproc_del()` shuts down, virtio subdevices are unbound (`rproc_remove_subdev()`).
- If an incoming mailbox doorbell is serviced by a deferred kworker after virtio endpoints are freed, `rproc_vq_interrupt()` can panic inside `virtqueue_add_inbuf()`.
- **Sunxi Solution**:
  1. `sunxi_rproc_stop()` halts the E906 core by asserting reset, stopping new doorbells.
  2. `sunxi_rproc_free_mbox()` frees the mailbox channel, then calls `cancel_work_sync(&priv->vq_work)` to flush pending work before subdevice resources are released.
  3. Inside `sunxi_rproc_vq_work()`, check `priv->rproc->state == RPROC_RUNNING` under `priv->rproc->lock`.

---

## 3. Mailbox Subsystem (`drivers/mailbox/mailbox.c`, `sun55i-msgbox.c`)

### A. Atomic Context & Spinlock Hierarchy
Inside the Linux mailbox core (`mailbox.c`):
```text
mbox_send_message(chan, data)
  └── spin_lock_irqsave(&chan->lock)
        └── chan->mbox->ops->send_data(chan, data)  <-- MUST BE STRICTLY ATOMIC!
```
- **Commit `3f39f5652037`**: Calling `pm_runtime_get_sync()` inside `send_data()` causes `BUG: sleeping function called from invalid context`.
- **Sun55i Rule**: `sun55i_msgbox_send_data()` performs purely non-blocking `writel()` into the remote processor's hardware FIFO without taking sleeping locks.

---

### B. Controller Pacing: `last_tx_done()` Semantics
- Mailbox channels configure `txdone_poll = true` and `txpoll_period = 1` (ms).
- **Drain vs Capacity**:
  - `last_tx_done()` must report `true` only when `count == 0` (empty).
  - If it reported `true` when `count < FIFO_MAX` (e.g. 7 out of 8 entries used), the mailbox framework would immediately dispatch the next message, causing FIFO overflow under bursty VirtIO RPMsg ping-pong traffic.

---

### C. Hardware Crossbar Routing & Crosstalk Isolation
The Allwinner sun55i message box connects 4 heterogeneous cores:
- ARM Cortex-A55 (`regs[0]`)
- Tensilica HiFi4 DSP (`regs[1]`)
- CPUS Power Core (`regs[2]`)
- XuanTie E906 RISC-V (`regs[3]`)

#### Route Calculation:
```c
/* Channel n (0-11) maps to: local_n, remote_id, remote_n */
void sun55i_chan_to_route(int n, int *local_n, int *p, int *remote_id, int *remote_n);
```
- Each processor receives in its own bank: ARM receives in `regs[0]`, E906 receives in `regs[3]`.
- When ARM transmits to E906 (channel 8-11), `sun55i_msgbox_send_data()` writes into `regs[3]` (the E906 bank).
- When E906 transmits to ARM, it writes into `regs[0]` (the ARM bank), triggering GIC SPI 0.
- **Critical Isolation Rule**: Linux only attaches an interrupt handler to its own bank (SPI 0). It never touches or services interrupts belonging to the DSP (SPI 1) or RISC-V (SPI 174).
