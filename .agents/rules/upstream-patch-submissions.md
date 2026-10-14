# Upstream Linux Kernel Patch Submission & Review Invariants

This rule governs all upstream patch creation, revision management, Devicetree schema validation, and mailing list dispatch across the **Cubie** project repository (`cubie-a5e` and `linux-cubie`).

Whenever preparing, reviewing, formatting, or sending patches to the Linux kernel mailing lists (`linux-remoteproc`, `linux-mailbox`, `linux-sunxi`, `devicetree`):

---

## 1. Single Source of Truth & Lessons Learned Reference

* **Mandatory Reference Ledger**:
  All agents MUST read and strictly adhere to **[cubie-a5e/upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md](../../upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md)** before writing driver code or formatting patches.
* **Audit Protocols**:
  Multi-stage adversarial review invariants are codified in **[cubie-a5e/upstream-remoteproc/scripts/sashiko_protocols.md](../../upstream-remoteproc/scripts/sashiko_protocols.md)**.
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

> These rules are decisions. Each one names its source. A review (AI or human) that
> disagrees does not change the code by itself: check it against the kernel source and the
> vendor BSP first, then update the rule here **and** the code together.
> Vendor BSP reference: `old-kernels/linux-aw2501/.git/modules/bsp` (Radxa allwinner-bsp,
> read with `git --git-dir=... show HEAD:<path>`): `drivers/remoteproc/sunxi_rproc.c`,
> `drivers/remoteproc/sunxi_rproc_e906_boot.c`, `drivers/msgbox/sunxi-msgbox.c`,
> `configs/linux-5.15/sun55iw3p1.dtsi`.

1. **Mailbox Pacing (decision for v3)**:
   - Controller: `txdone_poll = true`; `last_tx_done(chan)` returns `true` when the remote FIFO is empty (`count == 0`). Same policy as upstream `sun6i-msgbox`.
   - Client (remoteproc): `cl.knows_txdone = false`, `cl.tx_block = false`; never call `mbox_client_txdone()`.
   - Known cost: back-to-back kicks wait for a poll tick. The vendor BSP instead treats "FIFO not full" as done (5 ms poll) or uses the msgbox write IRQ (`txdone_irq`). Changing this is a deliberate post-v3 decision with a benchmark, not a review-driven flip.
2. **SMP Concurrency**:
   - MMIO status reads and FIFO drain operations in the msgbox ISR are enclosed in `spin_lock_irqsave(&mbox->lock, flags)`, and `startup()`/`shutdown()` change the IRQ-enable bit under the same lock.
3. **Mailbox rx callbacks are atomic**:
   - `include/linux/mailbox_client.h` documents `rx_callback` as an atomic callback, and the sun55i msgbox calls it from hardirq with `mbox->lock` held.
   - The virtio/rpmsg receive path takes mutexes, so the remoteproc client MUST defer `rproc_vq_interrupt()` to a work item (same as the vendor BSP and `stm32_rproc`).
   - PREEMPT_RT force-threads IRQ handlers and hides violations. Before each version, run once on a non-RT kernel with `CONFIG_DEBUG_ATOMIC_SLEEP=y` and `CONFIG_PROVE_LOCKING=y`.
4. **Teardown Order**:
   - `remove()`: `rproc_del()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()` (workqueue itself freed by devres).
   - Probe error path: `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()`.
   - Why: `rproc_del()` stops the core and removes virtio devices, which can still kick, so the tx channel must outlive it. Freeing the channels before cancelling means no rx callback can re-queue the work after the cancel. A work item that runs after the virtqueues are gone is safe because `rproc_vq_interrupt()` looks them up under the remoteproc vq SRCU (`remoteproc_virtio.c`).
   - The vendor BSP uses the same order (`rproc_del()` $\rightarrow$ free mailbox $\rightarrow$ `destroy_workqueue()`).
   - ❌ Calling `rproc_del()` last is wrong.
5. **ARM64 Memory Attributes**:
   - On-chip SRAM (`r_sram`, `r_sram1`): `devm_ioremap_wc()`, `mem->is_iomem = true`.
   - DRAM reserved regions (`no-map`): one mapping per physical range, with the **same** attribute the DMA pool uses for that memory (write-combining). Never map the same physical range twice with different attributes (WB vs WC vs device).
6. **Big-Endian Safe Unit Tests**:
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
