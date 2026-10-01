# Upstream Linux Kernel Patch Submission & Review Invariants

This rule governs all upstream patch creation, revision management, Devicetree schema validation, and mailing list dispatch across the **Cubie** project repository (`cubie-a5e` and `linux-cubie`).

Whenever preparing, reviewing, formatting, or sending patches to the Linux kernel mailing lists (`linux-remoteproc`, `linux-mailbox`, `linux-sunxi`, `devicetree`):

---

## 1. Single Source of Truth & Lessons Learned Reference

* **Mandatory Reference Ledger**:
  All agents MUST read and strictly adhere to **[cubie-a5e/upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md](../../cubie-a5e/upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md)** before writing driver code or formatting patches.
* **Audit Protocols**:
  Multi-stage adversarial review invariants are codified in **[cubie-a5e/upstream-remoteproc/scripts/sashiko_protocols.md](../../cubie-a5e/upstream-remoteproc/scripts/sashiko_protocols.md)**.
* **Adversarial Audit Runner**:
  Run `python3 cubie-a5e/upstream-remoteproc/scripts/run_adversarial_audit.py` before exporting any patch series. Output is stored directly in `v<N>/AUDIT.md`.

---

## 2. Devicetree Schema Rules (Krzysztof Kozlowski & Rob Herring Invariants)

1. **NO `enum` in `*-names`**:
   - `reg-names`, `clock-names`, and `reset-names` MUST be fixed, positional `items:` lists with `- const:` entries.
   - Hardware register banks do not have arbitrary permutations. Never use `items: enum: [...]`.
2. **Positional `items:` for Interrupts**:
   - Do NOT use free-form narrative text in `description:` explaining port order.
   - Define an explicit positional list for `interrupts` and `interrupt-names` with `minItems: 1`.
3. **No Redundant Descriptions or Formatting Pipes**:
   - Drop `description: bus clock` or `description: bus reset`.
   - Drop `|` pipes on single-line property descriptions.
   - Drop `status: true`.
4. **Pre-Submission Schema Validation**:
   - Always run:
     ```bash
     make -C linux-cubie dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/...
     ```
   - Must pass with **0 errors and 0 warnings**.

---

## 3. Subsystem Driver & Test Invariants

1. **Mailbox Pacing**:
   - `last_tx_done(chan)` must return `true` ONLY when the FIFO is empty (`count == 0`), proving the remote processor has consumed the message.
2. **SMP Concurrency**:
   - MMIO status reads and FIFO drain operations in hardirq context must be enclosed in `spin_lock_irqsave(&mbox->lock, flags)`.
3. **Teardown Order (Strict LIFO)**:
   - `devm_free_irq()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()` $\rightarrow$ `rproc_del()`.
   - Calling `rproc_del()` before `mbox_free_channel()` is a fatal virtqueue Use-After-Free bug.
4. **ARM64 Memory Attributes**:
   - On-chip SRAM (`r_sram`): `mem->is_iomem = true` (I/O memory).
   - System DRAM carveouts (`trace`, `vring`, CMA): `mem->is_iomem = false` (Normal WB Cacheable memory). Never double-map as device/non-cacheable.
5. **Big-Endian Safe Unit Tests**:
   - KUnit test mock MMIO assertions MUST use `readl()` accessors, NEVER direct array indexing (`mock[offset / 4]`).

---

## 4. Mailing List Threading & Dispatch Policy

1. **Fresh Top-Level Threads for New Revisions**:
   - **FORBIDDEN**: Never attach or thread new patch versions (v2, v3, etc.) under older threads using `--in-reply-to` or `--chain-reply-to`.
   - Each new version MUST be dispatched as an independent top-level thread with its own cover letter `[PATCH vX 0/N]`.
2. **Review Feedback Ingestion**:
   - When maintainer or bot reviews arrive, scrape the public-inbox mbox using:
     ```bash
     python3 cubie-a5e/upstream-remoteproc/scripts/fetch_lore_reviews.py --version v<N> --msgid <message-id>
     ```
   - Store all raw review emails in `v<N>/emails/` and document verbatim resolutions in `v<N>/COMMENTS.md`.
