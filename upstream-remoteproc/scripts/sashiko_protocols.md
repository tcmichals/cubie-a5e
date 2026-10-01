# Sashiko-Grade Multi-Stage Adversarial Kernel Review Protocols

This directory contains the four specialized review prompts and the adversarial gatekeeper prompt that replicate the Linux Foundation **Sashiko** multi-stage review protocol.

## Protocol Execution Flow

```text
               ┌────────────────────────────────────────────────────────┐
               │                Input Patch / Git Commit                │
               └───────────────────────────┬────────────────────────────┘
                                           │
         ┌───────────────────┬─────────────┴───────┬───────────────────┐
         ▼                   ▼                     ▼                   ▼
   ┌───────────┐       ┌───────────┐         ┌───────────┐       ┌───────────┐
   │  Stage 1  │       │  Stage 2  │         │  Stage 3  │       │  Stage 4  │
   │ Hardirq & │       │ Lifecycle │         │ Subsystem │       │ Intercon. │
   │ SMP Locks │       │& Teardown │         │ Contracts │       │ & Endian  │
   └─────┬─────┘       └─────┬─────┘         └─────┬─────┘       └─────┬─────┘
         │                   │                     │                   │
         └───────────────────┼─────────────────────┴───────────────────┘
                             ▼
               ┌───────────────────────────┐
               │          Stage 5          │
               │   Adversarial Gatekeeper  │
               │(Deduplication & Triage)   │
               └─────────────┬─────────────┘
                             ▼
                 Final High-Fidelity Report
```

---

### Stage 1: Hardirq & SMP Concurrency Auditor (`stage1_concurrency.md`)
**Role:** Adversarial Linux SMP & Interrupt Concurrency Specialist.
**Strict Focus:**
1. Assume SMP multi-core environment where CPU 0 and CPU 1 execute ISRs simultaneously.
2. Trace every shared structure variable (`flags`, `count`, `head`, `tail`, `enabled`).
3. Verify every MMIO read-modify-write cycle is enclosed in `spin_lock_irqsave(&lock, flags)` or atomic operations.
4. Flag any TOCTOU (Time-of-Check to Time-of-Use) window where status is read outside a lock, cleared, or looped without lock protection.
5. Check for hardirq execution bounds (no unbounded while loops that can lock a CPU core).

---

### Stage 2: Resource Lifecycle & Teardown Symmetry (`stage2_lifecycle.md`)
**Role:** Kernel Resource & Memory Lifetime Specialist.
**Strict Focus:**
1. **Probe Error Path Invariants**:
   - Verify reverse unwind order.
   - For every allocated resource, confirm corresponding free/disable call on error.
   - Check if clocks are disabled before MMIO register writes.
   - Guard every `mbox_free_channel()` call against `IS_ERR_OR_NULL()`.
2. **Device Remove & Module Unload Invariants**:
   - Order must strictly follow:
     `devm_free_irq()` / `disable_irq()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()` $\rightarrow$ `rproc_del()`.
   - Calling `rproc_del()` before `mbox_free_channel()` is a fatal virtqueue Use-After-Free bug.
   - Calling `cancel_work_sync()` before freeing mailbox channels allows a late IRQ to re-queue work after cancel returns.

---

### Stage 3: Subsystem Framework Contracts (`stage3_contracts.md`)
**Role:** Linux Subsystem Framework Invariant Specialist.
**Strict Focus:**
1. **Mailbox Subsystem (`drivers/mailbox/`)**:
   - `last_tx_done(chan)` semantics: Returns `true` ONLY when the previously transmitted message is completely consumed by the remote peer (`count == 0`), NOT when FIFO has available slots (`count < MAX`).
   - If controller handles TX polling, the client MUST NOT define `cl.knows_txdone = true` and call `mbox_client_txdone()`.
   - `send_data()`: Payload pointers must either be copied immediately to MMIO FIFO under lock or safely buffered; stack-local variable pointers must never escape asynchronously.
2. **RemoteProc Subsystem (`drivers/remoteproc/`)**:
   - `da_to_va()`: If an address translation entry (ATT) matches, it must never fall through to host physical address ranges.
   - Check `da + len` 64-bit integer overflow protection on all segments.
   - DT reserved-memory carveouts: Do not double-map memory with conflicting ARM64 MMU attributes (`MEMREMAP_WB` vs `ioremap_wc`).

---

### Stage 4: Hardware Interconnect, MMIO & Endianness (`stage4_hardware.md`)
**Role:** SoC Silicon Fabric & Hardware Architecture Specialist.
**Strict Focus:**
1. **Interconnect Posted Writes**:
   - Register writes to execution boot vectors (`STA_ADD_REG`) or clock gating must be flushed with a dummy `readl()` read-back before deasserting execution resets.
2. **Reset Ordering**:
   - Never fall back to un-gating a bus reset (`rst_cfg`) as an execution reset (`rst_core`). Core execution must strictly require dedicated core reset.
3. **KUnit Mock Register Endianness**:
   - Direct array index reads (`regs[i]`) on mock MMIO fixtures after `writel()` will fail on Big-Endian hosts due to byte-swapping. All test assertions must use `readl()`.

---

### Stage 5: Adversarial Gatekeeper & Deduplication (`stage5_gatekeeper.md`)
**Role:** Senior Linux Subsystem Maintainer & Bug Triage Gatekeeper.
**Strict Focus:**
1. Consolidate raw findings from Stages 1 through 4.
2. Interrogate every finding:
   - Is this an actual bug or a misunderstanding of framework internals?
   - Can this path be reached in practice?
   - Is it already protected by an outer subsystem lock (e.g. `rproc->lock`)?
3. Eliminate false positives.
4. Output verified findings classified strictly as `[High]`, `[Medium]`, or `[Low]` with filename, function, exact line number, and kernel-standard remediation diff.
