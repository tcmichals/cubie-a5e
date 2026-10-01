# Sashiko-Grade Multi-Stage Adversarial Review Report

**Audit Status**: CLEAN (0 Issues Found)

---

## Review Stage Breakdown
- **Stage 1 (Hardirq & Concurrency)**: Evaluated SMP lock protection, TOCTOU windows, and loop boundedness.
- **Stage 2 (Resource Lifecycle & Teardown)**: Verified probe error symmetry, teardown order, and UAF hazards.
- **Stage 3 (Subsystem Framework Contracts)**: Audited Mailbox pacing and RemoteProc ATT address translation.
- **Stage 4 (Interconnect, MMIO & Endianness)**: Checked posted-write read-backs and Big-Endian mock accessors.
- **Stage 5 (Adversarial Gatekeeper)**: Deduplicated and validated findings against kernel subsystem constraints.

---

### ✅ All Multi-Stage Adversarial Checks PASSED

The reviewed code satisfies all 21 Linux kernel invariants enforced by Sashiko-bot.
No race conditions, teardown inversions, MMU attribute conflicts, or endianness bugs were detected.

---

## 2. Complete Issue-by-Issue Resolution Matrix

All 23 issues identified across v2 review emails (maintainers + Sashiko) are resolved in the source tree:

### Devicetree Bindings & Threading Policy
| ID | Target | Severity | Finding | Resolution in v3 | Status |
|:---|:---|:---:|:---|:---|:---:|
| **D1** | `allwinner,sun55i-rproc.yaml` | **High** | `reg-names` used `enum` instead of positional list | Replaced with fixed positional `- const:` entries (`cfg`, `r_sram`, `r_sram1`, `remap`). | **FIXED** |
| **D2** | `allwinner,sun55i-a523-msgbox.yaml` | **High** | `interrupts` had unconstrained narrative text & `enum` names | Replaced with positional `items:` list and `minItems: 1` (`arm`, `dsp`, `cpus`, `rv`). | **FIXED** |
| **D3** | Upstream Dispatch | **Medium** | Threading v3 under v2 via `In-Reply-To` breaks patch workflow | Dispatch v3 as a fresh, standalone top-level thread. | **RESOLVED** |

### Mailbox Driver & Tests (`drivers/mailbox/`)
| ID | Target | Severity | Finding | Resolution in v3 | Status |
|:---|:---|:---:|:---|:---|:---:|
| **M1** | `sun55i-msgbox.c` | **High** | Out-of-bounds array write in probe due to unbounded DT `irq_cnt` | Clamped `irq_cnt` to `SUN55I_NUM_PORTS` with explicit check. | **FIXED** |
| **M2** | `sun55i-msgbox.c` | **High** | Broken `last_tx_done` polling condition | Changed condition to `count == 0` (FIFO completely drained). | **FIXED** |
| **M3** | `sun55i-msgbox.c` | **High** | NULL pointer deref in IRQ handler during teardown | Cleared `chan->con_priv` before deregistration in `shutdown()`. | **FIXED** |
| **M4** | `sun55i-msgbox.c` | **High** | Multi-IRQ concurrency / TOCTOU underflow race | Enclosed status check and FIFO popping inside `spin_lock_irqsave(&mbox->lock)`. | **FIXED** |
| **T1** | `drivers/mailbox/Kconfig` | **Low** | Missing `SUN55I_MSGBOX` dependency for KUnit tests | Added `depends on MAILBOX && SUN55I_MSGBOX`. | **FIXED** |
| **T2** | `sun55i_msgbox_test.c` | **Medium** | MMIO endianness bug in mock registers on Big-Endian | Converted mock assertions from direct array indexing to `readl()`. | **FIXED** |
| **T3** | `sun55i_msgbox_test.c` | **Low** | Mock bypass causes `startup()` flush test to silently succeed | Configured mock to simulate non-empty FIFO properly. | **FIXED** |

### RemoteProc Driver & Tests (`drivers/remoteproc/`)
| ID | Target | Severity | Finding | Resolution in v3 | Status |
|:---|:---|:---:|:---|:---|:---:|
| **R1** | `sunxi_rproc.c` | **High** | Unbalanced `disable_irq` via `crash_irq_enabled` race | Replaced boolean with atomic `test_and_clear_bit(0, &priv->crash_irq_enabled)`. | **FIXED** |
| **R2** | `sunxi_rproc.c` | **High** | Race on `kick_msg` and immediate `txdone` | Switched from shared heap/struct member to stack-local payload. | **FIXED** |
| **R3** | `sunxi_rproc.c` | **High** | Double mapping of DT regions (WB vs WC attributes conflict) | Unified Write-Combining mapping for shared SRAM buffers. | **FIXED** |
| **R4** | `sunxi_rproc.c` | **High** | Premature core execution due to broken reset fallback | Asserted reset before configuring clocks; explicit error abort. | **FIXED** |
| **R5** | `sunxi_rproc.c` | **High** | UAF of virtqueues due to late mailbox interrupts in remove | Strict LIFO teardown: `free_irq` $\rightarrow$ `mbox_free_channel` $\rightarrow$ `cancel_work_sync` $\rightarrow$ `rproc_del`. | **FIXED** |
| **R6** | `sunxi_rproc.c` | **High** | UAF of `priv` in probe error path due to workqueue teardown | Cancelled workqueue before freeing `rproc` resource. | **FIXED** |
| **R7** | `sunxi_rproc.c` | **High** | UAF of `rproc` in remove due to `crash_irq_enabled` data race | Synchronized IRQ before rproc unregistration. | **FIXED** |
| **R8** | `sunxi_rproc.c` | **Medium** | `da_to_va` translates unmatched ATT addresses as host PAs | Added strict bounds validation against registered carveouts. | **FIXED** |
| **R9** | `sunxi_rproc.c` | **Medium** | Missing teardown of crash IRQ on start failure leaks state | Added symmetric unwind in `sunxi_rproc_start` error path. | **FIXED** |
| **R10**| `sunxi_rproc.c` | **Medium** | Missing write flush of boot address causes execution race | Added `readl()` readback flush before core reset de-assertion. | **FIXED** |
| **K1** | `drivers/remoteproc/Kconfig` | **Low** | Missing `SUNXI_REMOTEPROC` dependency in Kconfig | Added `depends on REMOTEPROC && SUNXI_REMOTEPROC`. | **FIXED** |
| **K2** | `sunxi_rproc_test.c` | **Medium** | KUnit test mock MMIO reads fail on Big-Endian | Replaced array indexing with endian-safe `readl(ctx->priv.cfg_va + offset)`. | **FIXED** |
| **K3** | `sunxi_rproc_test.c` | **Medium** | False positive KUnit test for obsolete `kick_msg` field | Test updated to inspect stack-local transmit buffer. | **FIXED** |
