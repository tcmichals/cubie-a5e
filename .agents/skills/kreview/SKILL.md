---
name: kreview
description: Comprehensive Linux kernel patch and driver review using Linux Foundation Sashiko protocols, Smatch/Sparse patterns, and DeviceTree schema rules.
---

# Kernel Patch Deep-Dive Review Protocol (/kreview)

When this skill is activated (via `/kreview` or by asking to review a commit/series), execute an exhaustive adversarial analysis on the target commit(s) in `linux-cubie`.

## Target Selection
- If argument provided (e.g. `/kreview HEAD~1` or `/kreview HEAD~2..HEAD`), use that range.
- Default: `HEAD`.

## 4-Tier Screening Pipeline

### Tier 1: Deterministic Schema & Binding Rules (DeviceTree)
Check every `.yaml` binding file against upstream maintainer rules:
1. **Positional Constraint Check**:
   - Are `reg-names` and `interrupt-names` defined using fixed positional items (`items: [ { const: foo }, { const: bar } ]`)?
   - **FAIL** if `enum:` is used across multiple items without a positional definition.
2. **Exclusivity**: Does the schema use `additionalProperties: false` (or `unevaluatedProperties: false` if inheriting)?
3. **Example Validation**: Does the DTS example match the binding constraints exactly?

### Tier 2: Kernel Memory, Concurrency & Hardware Invariants (C Code)
1. **FIFO & Controller Pacing**:
   - For mailbox drivers: does `last_tx_done()` check `count == 0`?
   - **FAIL** if `< FIFO_MAX` is used when clients expect full delivery.
2. **SMP Race & Hardirq Locking**:
   - Are shared hardware registers accessed in ISR guarded by `spin_lock_irqsave()`?
   - Can multiple CPUs call `mbox_send_message()` concurrently?
3. **Teardown & IRQ Synchronization**:
   - In `remove()`: are hardware IRQs masked AND synchronized via `synchronize_irq()` BEFORE `mbox_controller_unregister()` or `rproc_del()`?
4. **DMA & Cache Attribute Coherency**:
   - Are trace and DRAM buffers mapped with `is_iomem = false` to prevent ARM64 Write-Back vs Write-Combining MMU conflicts?
5. **Bus Flush & Reset Ordering**:
   - Are register writes followed by a dummy `readl()` flush before releasing hardware resets?

### Tier 3: Error Unwinding & Resource Lifetimes
1. **Probe Error Path**:
   - Trace every `goto err_*` in `probe()`.
   - Ensure reverse order of allocation.
   - Verify `devm_` managed vs manually allocated resources are not double-freed.
2. **ERR_PTR checks**:
   - Ensure pointers returned by `devm_clk_get()`, `devm_reset_control_get()`, etc., use `IS_ERR()` before dereferencing.

### Tier 4: Adversarial False-Positive Filtering
Before reporting any finding:
- Cross-reference against `false-positive-guide.md`.
- Formulate a hypothesis to DISPROVE the bug.
- Only report confirmed, high-confidence issues with exact line numbers and proposed fixes.

## Output Format
Output the findings according to LKML standards:
- List of reviewed commits (hash, subject, author).
- Pass/Fail status for each Tier.
- Inline patch comments with `+`/`-` remediation diffs if bugs are found.
- Final Upstream Readiness Verdict (CLEAN or ACTION REQUIRED).
