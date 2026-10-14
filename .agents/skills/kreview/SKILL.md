---
name: kreview
description: Comprehensive Linux kernel patch and driver review using Linux Foundation Sashiko protocols, Smatch/Sparse patterns, DeviceTree schema rules, Peer Driver benchmarking, and Kernel Lore maintainer patterns.
---

# Kernel Patch Deep-Dive Review Protocol (/kreview)

When this skill is activated (via `/kreview` or by asking to review a commit/series), execute an exhaustive adversarial analysis on the target commit(s) in `linux-cubie`.

Before starting the review, consult the detailed references:
- **Peer Driver Benchmarks**: [references/peer_drivers.md](references/peer_drivers.md)
- **Kernel Lore Submission Traps**: [references/lore_guidelines.md](references/lore_guidelines.md)
- **Project Design Decisions**: [upstream-remoteproc/DESIGN_DECISIONS.md](file:///home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/upstream-remoteproc/DESIGN_DECISIONS.md)

---

## Target Selection
- If argument provided (e.g. `/kreview HEAD~1` or `/kreview HEAD~2..HEAD`), use that range.
- Default: `HEAD` on `linux-cubie`.

---

## 6-Tier Adversarial Screening Pipeline

### Tier 1: Deterministic Schema & Binding Rules (DeviceTree)
Check every `.yaml` binding file against upstream maintainer rules (Krzysztof Kozlowski / Rob Herring):
1. **Positional Constraint Check**:
   - Are `reg-names`, `clock-names`, `reset-names`, and `interrupt-names` defined using fixed positional items (`items: [ - const: foo, - const: bar ]`)?
   - **FAIL** if `enum:` is used across multiple items without a positional definition.
2. **Exclusivity**: Does the schema use `additionalProperties: false` (or `unevaluatedProperties: false` if inheriting)?
3. **No Driver Prose**: Ensure descriptions describe physical hardware, not Linux internal driver concepts (workqueues, buffers).
4. **Subject Line Hygiene**: Drop redundant words like "schema" or "bindings" from patch subjects.
5. **Example Validation**: Does the DTS example match the binding constraints exactly?

### Tier 2: Kernel Memory, Concurrency & Hardware Invariants (C Code)
1. **FIFO & Controller Pacing**:
   - For mailbox drivers: does `last_tx_done()` check `count == 0`?
   - **FAIL** if `< FIFO_MAX` is used when clients expect full delivery.
2. **Atomic Context & Workqueue Deferral**:
   - Mailbox `rx_callback` runs in atomic/hardirq context.
   - **FAIL** if `rproc_vq_interrupt()` or any sleeping function is invoked directly in `rx_callback`. It must be deferred to an ordered workqueue (`queue_work(priv->workqueue, &priv->vq_work)`).
3. **SMP Race & Hardirq Locking**:
   - Are shared hardware registers accessed in ISR guarded by `spin_lock_irqsave()`?
   - Are TOCTOU windows closed by acknowledging the hardware pending bit BEFORE draining FIFOs?
   - Are FIFO drain loops capped at `FIFO_MAX` to prevent CPU soft-lockups?
4. **DMA & Cache Attribute Coherency**:
   - Are trace and DRAM buffers mapped with matching attributes (`MEMREMAP_WC`) to prevent ARM64 Write-Back vs Write-Combining aliasing?
   - Are local SRAMs mapped with `is_iomem = true` and `devm_ioremap_resource_wc()`?
5. **Bus Flush & Reset Ordering**:
   - Are MMIO register writes (`STA_ADD`, `REMAP`) followed by an explicit `readl()` flush before releasing hardware resets?

### Tier 3: Error Unwinding & Resource Lifetimes
1. **Probe Error Path**:
   - Trace every `goto err_*` in `probe()`. Ensure reverse order of allocation.
   - Verify `devm_` managed vs manually allocated resources are not double-freed.
2. **ERR_PTR & Null Checks**:
   - Ensure pointers returned by `devm_clk_get()`, `devm_reset_control_get()`, `devm_ioremap_resource()`, and `devm_memremap()` use `IS_ERR()` before dereferencing.
3. **Teardown Ordering in `remove()`**:
   - **Rule**: `rproc_del()` must be called BEFORE `mbox_free_channel()`.
   - **Rule**: `cancel_work_sync(&priv->vq_work)` must be called AFTER `mbox_free_channel()` so no new work can be scheduled.

### Tier 4: SMP Concurrency & Lifecycle Race Matrix
Audit cross-core concurrency and shutdown interactions:
1. **Stop vs. Mailbox RX**: When `rproc_stop()` asserts core reset, ensure any in-flight `vq_work` cannot access invalidated resources.
2. **Stop vs. Kick**: Ensure `rproc_kick()` does not access freed mailbox pointers during unbind.
3. **Crash IRQ vs. Shutdown**: Verify `disable_irq()` is executed without holding spinlocks to prevent `might_sleep()` sleep-in-atomic crashes.

### Tier 5: Peer Driver & Schema Comparative Cross-Audit
Directly cross-reference the driver logic against upstream reference implementations:
1. **RemoteProc Peers**: Compare ATT translation and carveout registration with `drivers/remoteproc/imx_rproc.c` and `drivers/remoteproc/stm32_rproc.c`.
2. **Mailbox Peers**: Compare crossbar channel routing with `drivers/mailbox/sun6i-msgbox.c` and `drivers/mailbox/arm_mhu.c`.
3. **Divergence Verification**: For every architectural difference between our driver and the peer drivers, verify whether the divergence is required by hardware differences or represents a defect.

### Tier 6: Adversarial False-Positive Filtering & Lore Grounding
Before reporting any finding:
1. **Cross-reference against [DESIGN_DECISIONS.md](file:///home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/upstream-remoteproc/DESIGN_DECISIONS.md)**: Do not report deliberate project decisions as defects (e.g. keeping DRAM firmware, sharing PRCM remap bit 1, keeping A733 placeholders).
2. **Formulate a hypothesis to DISPROVE the bug**: Inspect real kernel base code (`linux-next`) to verify if the core framework already protects against the scenario (e.g. SRCU protection in `remoteproc_virtio.c`).
3. **Lore Search (Optional)**: If evaluating novel maintainer feedback, check `lore.kernel.org` patterns for subsystem maintainers (Bjorn Andersson, Mathieu Poirier, Jassi Brar, Krzysztof Kozlowski).

---

## Output Format
Output the findings strictly according to LKML standards:
- **List of reviewed commits** (hash, subject, author).
- **Pass/Fail status for each of the 6 Tiers**.
- **Peer Divergence Matrix**: Summary of how the code compares to mainline peers (`imx_rproc`, `stm32_rproc`).
- **Inline patch comments** with `+`/`-` remediation diffs if bugs are found.
- **Final Upstream Readiness Verdict** (`CLEAN` or `ACTION REQUIRED`).
