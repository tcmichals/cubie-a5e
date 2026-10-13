# RemoteProc and Mailbox Subsystem Review Guide

This guide defines correctness invariants for Linux kernel drivers in `drivers/remoteproc/` and `drivers/mailbox/`.

---

## Invariant 1: Carveouts from DT must be registered in `.prepare()`, not only in `probe()`
- **Core Function to Load**: `drivers/remoteproc/remoteproc_core.c` (`rproc_resource_cleanup()`, `rproc_shutdown()`), `drivers/remoteproc/stm32_rproc.c` (`stm32_rproc_prepare()`).
- **Rationale**: `rproc_resource_cleanup()` frees every entry in `rproc->carveouts` on every core stop, crash recovery, or failed boot. If carveouts are only registered during driver probe, subsequent starts or crash recovery cycles will find carveouts completely missing.
- **REPORT as bug**: Any driver calling `rproc_add_carveout()` in `probe()` for memory regions needed across multiple start/stop lifecycles without re-registering them in `.prepare()`.

---

## Invariant 2: `remove()` must call `rproc_del()` before freeing mailbox channels
- **Core Function to Load**: `drivers/remoteproc/remoteproc_core.c` (`rproc_del()`, `rproc_shutdown()`, `rproc_remove_virtio_dev()`).
- **Rationale**: `rproc_del()` stops a running core and unregisters virtio/rpmsg devices. During virtio device removal, shutdown kicks (`.kick()`) are still sent to the coprocessor. If mailbox channels are freed before `rproc_del()`, kicks will access freed or NULL mailbox channels.
- **REPORT as bug**: Any driver freeing mailbox channels (`mbox_free_channel()`) before calling `rproc_del()` in its `remove()` callback.

---

## Invariant 3: `.start()` and `.stop()` are serialized by `rproc->lock`; no sleeping under spinlocks
- **Core Function to Load**: `drivers/remoteproc/remoteproc_core.c` (`rproc_boot()`, `rproc_shutdown()`), `kernel/irq/manage.c` (`synchronize_irq()`, `might_sleep()`).
- **Rationale**: The remoteproc core holds `rproc->lock` (a mutex) across calls to `.start()` and `.stop()`. No driver spinlock is needed to serialize start and stop against each other. Furthermore, `disable_irq()` calls `synchronize_irq()`, which sleeps. Calling `disable_irq()` under a spinlock deadlocks.
- **REPORT as bug**: Any driver acquiring a spinlock around `disable_irq()`, `synchronize_irq()`, `cancel_work_sync()`, or other sleeping functions in remoteproc callbacks.

---

## Invariant 4: `.prepare()` failure cleanup must be self-contained
- **Core Function to Load**: `drivers/remoteproc/remoteproc_core.c` (`rproc_fw_boot()`).
- **Rationale**: If `.prepare()` fails, the remoteproc core jumps to error exit without calling `rproc_resource_cleanup()`. Any memory allocations or carveout entries allocated during prepare must be cleaned up on prepare's own error unwind path.
- **REPORT as bug**: Any driver allocating resources in `.prepare()` that leaves them on rproc lists or unfreed when prepare returns an error code.

---

## Invariant 5: Mailbox payloads in `txdone_poll` must outlive `mbox_send_message()`
- **Core Function to Load**: `drivers/mailbox/mailbox.c` (`mbox_send_message()`, `txdone_hrtimer()`, `mbox_controller_unregister()`).
- **Rationale**: With `txdone_poll = true`, the mailbox controller stores the message pointer passed to `mbox_send_message()` and dereferences it until transmission is confirmed. The payload must reside in stable storage (struct member, static table, or heap), never a stack-local variable. Furthermore, `mbox_controller_unregister()` invokes `shutdown()` for all channels; the ISR must not process channels whose RX interrupt is masked.
- **REPORT as bug**: Any driver passing stack pointers to `mbox_send_message()`, or checking FIFO capacity (`count < FIFO_MAX`) instead of full drain (`count == 0`) in `last_tx_done()`.

---

## Invariant 6: Controllers must request only interrupts serviced by their ISR
- **Core Function to Load**: `drivers/mailbox/mailbox.c`, SoC Message Box specification.
- **Rationale**: In multi-processor message boxes, peer register banks generate interrupts for other cores (DSP, CPUS, RISC-V). If the host OS requests all interrupts, peer interrupts will trigger the host ISR, which returns `IRQ_NONE` and eventually causes the kernel to disable the IRQ line as spurious.
- **REPORT as bug**: Any mailbox or peripheral driver requesting foreign/peer coprocessor interrupt lines that its handler does not service.

---

## Invariant 7: Clock and reset ownership must match hardware register access
- **Core Function to Load**: Subsystem CCU/Reset headers and device tree node definitions.
- **Rationale**: Any register bank, FIFO, or MMIO window touched by a driver must have its clock enabled and reset deasserted by the device node owning that transaction. Drivers must not touch peripheral banks whose clocks are owned or managed by other nodes.
- **REPORT as bug**: Any driver accessing multiple register banks without enabling clocks and deasserting resets for each respective bank.
