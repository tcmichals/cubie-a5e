# Sashiko Adversarial Linux Kernel Review Protocols & Official Tooling Guide

This document captures the official architecture, upstream repositories, CLI usage, and formal review protocols of the Linux Foundation **Sashiko** code review system.

---

## 1. Upstream Project References

* **Official GitHub Repository**: [`https://github.com/sashiko-dev/sashiko`](https://github.com/sashiko-dev/sashiko)
* **Web Review Dashboard**: [`https://sashiko.dev`](https://sashiko.dev)
* **Linux Foundation Mailing List**: `sashiko@lists.linux.dev` (automated reviews posted from `sashiko-bot@kernel.org` / `sashiko-reviews@lists.linux.dev`)
* **Lead Architect**: Roman Gushchin (Linux kernel engineer at Google)
* **NVIDIA Local CLI Companion**: [`https://github.com/NVIDIA/boro`](https://github.com/NVIDIA/boro) (interactive local patch mending tool)

---

## 2. Running Official Sashiko Locally

Sashiko provides a standalone Rust CLI (`sashiko review`) that runs its multi-stage LLM review pipeline directly over your local git worktree before you submit patches:

### Installation
```bash
# Requires Rust 1.90+ (curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh)
cargo install sashiko
```

### Configuration
```bash
sashiko init
# Supports Gemini (default), Claude, Vertex AI, AWS Bedrock, OpenAI
export LLM_API_KEY="your-gemini-or-claude-api-key"
```

### Execution on Local Commits
```bash
cd /path/to/linux-cubie

# Review the 7 commits of the RemoteProc & Mailbox series:
sashiko review HEAD~7..HEAD

# Review working tree uncommitted changes:
sashiko review
```

---

## 3. Official Review Stages Architecture

Sashiko executes a declarative multi-stage workflow defined in `src/workflows/linux_patch_review.rs`:

```text
               ┌────────────────────────────────────────────────────────┐
               │           Input Patch Series / Git Commit Range        │
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

## 4. Specialized Review Invariants & Prompt Sources

The official prompt templates live in `sashiko-dev/sashiko` under `third_party/prompts/kernel/subsystem/`. Key subsystem invariants to verify before every submission:

### Stage 1: Hardirq & SMP Concurrency Auditor (`subsystem/locking.md`)
* **Source**: `third_party/prompts/kernel/subsystem/locking.md`
* **Invariants**:
  1. Multi-Core SMP concurrency: Assume CPU 0 and CPU 1 execute the same or related ISR simultaneously.
  2. MMIO read-modify-write cycles must be enclosed in `spin_lock_irqsave(&lock, flags)` or atomic operations.
  3. No TOCTOU (Time-of-Check to Time-of-Use) windows where register status is checked outside the lock and then cleared/read inside the lock.
  4. Execution boundedness: No unbounded while loops in hardirq context without loop iteration limits.

### Stage 2: Resource Lifecycle & Teardown Symmetry (`subsystem/workqueue.md`, `cleanup.md`)
* **Source**: `third_party/prompts/kernel/subsystem/workqueue.md` and `cleanup.md`
* **Invariants**:
  1. Probe error reverse unwind: Every allocated resource must have a corresponding free/disable call in reverse order on error.
  2. Clocks must remain enabled during MMIO shutdown/clear register writes.
  3. Strict LIFO Teardown:
     `free_irq()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()` $\rightarrow$ `rproc_del()`.
     * Calling `rproc_del()` before `mbox_free_channel()` is a fatal virtqueue Use-After-Free.
     * Calling `cancel_work_sync()` before freeing mailbox channels allows a late IRQ to re-queue work after cancel returns.
  4. Guard every `mbox_free_channel()` call against `!IS_ERR_OR_NULL()`.

### Stage 3: Subsystem Framework Contracts (`dt-bindings.md`, Framework Pacing)
* **Source**: `third_party/prompts/kernel/subsystem/dt-bindings.md`
* **Invariants**:
  1. **Mailbox Pacing**: `last_tx_done(chan)` returns `true` ONLY when the previously transmitted message is completely consumed by the remote peer (`count == 0`), NOT when FIFO merely has space (`count < MAX`).
  2. **Device Tree Binding Schemas**:
     * NEVER use `enum` in `reg-names`, `clock-names`, or `reset-names`. Hardware registers have fixed addresses; always use positional `items:` lists with `- const:`.
     * Never use freeform narrative text in `description:` for interrupts. Use positional `items:` with `minItems: 1`.
  3. **RemoteProc ATT & Carveout Translation**:
     * Strict bounds checking on `da + len` to prevent 64-bit integer overflow.
     * Memory attributes parity: On-chip SRAM (`mem->is_iomem = true`), System DDR carveouts (`mem->is_iomem = false`). Never double-map as device/non-cacheable.

### Stage 4: Hardware Interconnect, MMIO & Endianness (`subsystem/io-accessors.md`)
* **Source**: `third_party/prompts/kernel/subsystem/io-accessors.md`
* **Invariants**:
  1. **Interconnect Posted Writes**: Register writes to execution boot vectors (`STA_ADD_REG`) or clock gating must be flushed with a dummy `readl()` read-back before deasserting execution resets.
  2. **Reset Ordering**: Execution resets must never fall back to bus resets. Core execution requires dedicated core reset control.
  3. **Big-Endian Safe MMIO**:
     * `writel()` byte-swaps on Big-Endian architectures.
     * In KUnit test mocks, NEVER inspect registers by direct array indexing (`mock[offset / 4]`). Always read through `readl()`.
     * Never mix stream accessors (`writesl()`) with register accessors (`writel()`) on the same FIFO.

### Stage 5: Adversarial Gatekeeper & Deduplication
* Consolidate raw findings across all stages.
* Eliminate false positives by cross-checking subsystem locking invariants (e.g. `rproc->lock`).
* Output verified findings classified strictly as `[High]`, `[Medium]`, or `[Low]` with filename, function, exact line number, and remediation diff.

---

## 5. Local Audit Automation Script

The Python script [`cubie-a5e/upstream-remoteproc/scripts/run_adversarial_audit.py`](run_adversarial_audit.py) codifies these 21 invariants and runs them locally without requiring external LLM API calls.

* **Run Command**:
  ```bash
  python3 cubie-a5e/upstream-remoteproc/scripts/run_adversarial_audit.py
  ```
* **Output Destination**:
  Results are written automatically to `cubie-a5e/upstream-remoteproc/v3/AUDIT.md`.
