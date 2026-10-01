# Upstream v3 Verification & Adversarial Audit Results

- **Audit Date**: 2026-10-01 13:48:42 UTC
- **Baseline Branch**: `cubie-linux-7.1` (Commit `eeffd74f0af7`)
- **Overall Audit Status**: **PASSED / CLEAN (0 Issues Found)**
- **Verification Matrix Reference**: [REVIEW_TRACKER.md](REVIEW_TRACKER.md)
- **Archived Mailing List Feedback**: [../v2/COMMENTS.md](../v2/COMMENTS.md)

---

## 1. Adversarial Audit Multi-Stage Breakdown (`run_adversarial_audit.py`)

The automated 5-stage adversarial audit tool models the strict review heuristics enforced by Sashiko-bot and kernel subsystem maintainers:

| Stage | Focus Area | Checks Evaluated | Status |
|:-----:|:-----------|:-----------------|:------:|
| **Stage 1** | **Hardirq & Concurrency** | SMP spinlock protection on shared MMIO registers, TOCTOU race windows in status clearing, bounded FIFO drain loops. | **PASS** |
| **Stage 2** | **Resource Lifecycle & Teardown** | Probe error unwind symmetry, strict LIFO teardown ordering (`free_irq` $\rightarrow$ `mbox_free_channel` $\rightarrow$ `cancel_work_sync` $\rightarrow$ `rproc_del`), UAF hazards. | **PASS** |
| **Stage 3** | **Subsystem Framework Contracts** | Mailbox transmit pacing (`last_tx_done` returns `count == 0`), RemoteProc ATT address translation bounds checks, `da_to_va` validation against registered carveouts. | **PASS** |
| **Stage 4** | **Interconnect, MMIO & Endianness** | Posted-write read-backs for register flushes, Big-Endian safe `readl()` accessors on KUnit mock registers, ARM64 cacheable memory attributes (Device / Normal Non-Cacheable). | **PASS** |
| **Stage 5** | **Adversarial Gatekeeper** | Deduplication, false-positive suppression, and strict validation against mainline Linux subsystem constraints. | **PASS** |

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

---

## 3. Subsystem Build & Static Tool Validation

1. **Devicetree Schema Check**:
   ```bash
   make dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml DT_SCHEMA_FILES=Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
   ```
   - **Result**: **0 errors, 0 warnings** (schemas compile cleanly to `.dtb`).

2. **Patch & Code Formatting Strict Check**:
   ```bash
   ./scripts/checkpatch.pl --strict -f drivers/mailbox/sun55i-msgbox.c drivers/mailbox/sun55i_msgbox_test.c drivers/remoteproc/sunxi_rproc.c drivers/remoteproc/sunxi_rproc_test.c Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
   ```
   - **Result**: **0 errors, 0 warnings, 0 checks** across all 6 files.

3. **Kernel & Image Build**:
   ```bash
   make -C bld.a5e linux-rebuild
   ```
   - **Result**: **Exit 0**. Installed fresh `Image`, modules, and DTBOs to `bld.a5e/images/`.
