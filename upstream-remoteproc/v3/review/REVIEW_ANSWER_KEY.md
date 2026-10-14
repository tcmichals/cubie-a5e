# Review Benchmark Answer Key: Known Kernel Issues

This document defines the ground-truth benchmark of known architectural bugs, subsystem contract violations, and binding issues in the Allwinner A523 msgbox + remoteproc patch series.

Use this answer key to score automated review runs (Claude, Gemini, Sashiko, semcode) and prompt versions for:
- **Detection Rate (Recall)**: How many of these known issues were identified?
- **Citation Quality**: Did the finding cite the authoritative Linux kernel core code or hardware documentation that proves it?
- **False Positive Rate**: How many spurious or incorrect findings were generated?

---

## 1. The Core Kernel Findings

| ID | Issue | Authoritative Kernel Source / Hardware Evidence | Failure Mode |
|:---|:---|:---|:---|
| **ISSUE-01** | `disable_irq()` called under spinlock in `stop()` | `kernel/irq/manage.c` (`synchronize_irq()` calls `might_sleep()`) | Sleep in atomic context / deadlock on SMP when threaded handler waits on same lock. |
| **ISSUE-02** | DT carveouts lost after first stop/reboot | `drivers/remoteproc/remoteproc_core.c` (`rproc_resource_cleanup()` frees `rproc->carveouts`) | On second start, restart, or crash recovery, named carveouts are gone. Carveouts belong in `.prepare()`. |
| **ISSUE-03** | Inverted teardown: mailbox freed before `rproc_del()` | `drivers/remoteproc/remoteproc_core.c` (`rproc_del()` $\rightarrow$ `rproc_shutdown()` $\rightarrow$ `rproc_remove_virtio_dev()`) | VirtIO removal and shutdown still issue kicks via `.kick()`, causing use-after-free on freed mailbox channels. |
| **ISSUE-04** | Spurious IRQ storm from foreign mailbox banks | SoC Register Map + `sun55i-msgbox.c` ISR (`readl(regs[0])` only) | Linux host requests peer DSP/CPUS/RISC-V IRQs. When remote cores write, host ISR returns `IRQ_NONE` until disabled. |
| **ISSUE-05** | Linux claims the other processors' msgbox banks (clocks/resets) | Vendor DT and board-tested 7.1 DTS use only `CLK_BUS_MSGBOX` / `RST_BUS_MSGBOX`; [DESIGN_DECISIONS.md](../../DESIGN_DECISIONS.md) D12 | Taking the DSP/CPUS/RV bank resets means unbinding the msgbox asserts resets that belong to running remote processors. (Corrected 2026-10-14: the earlier entry said the opposite.) |
| **ISSUE-06** | Memory region collision: `dram`/`vram` overwrite | `Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml` vs driver `strstr()` | Both regions hit `strstr(name, "dram") \|\| strstr(name, "vram")`, causing `priv->dram_va` to point to whichever came last. |
| **ISSUE-07** | `devm_memremap()` returns `ERR_PTR`, not `NULL` | `include/linux/io.h` / `kernel/memremap.c` (`devm_memremap()` API contract) | `if (!va)` checks allow `ERR_PTR(-ENOMEM)` through into `rproc_add_carveout()`, causing kernel crash on dereference. |
| **ISSUE-08** | Undocumented / ad-hoc DT properties in driver | Binding YAML vs `sunxi_rproc.c` lookup of `"sram"` / `"sram-for-cpux"` | Driver parsed legacy strings not defined in binding schema; remap register offset was hardcoded instead of direct resource map. |
| **ISSUE-09** | A733 / sun60i untested dead code in series | Series commits vs drivers (`no A733 compatible, no DT, E902 != E906`) | Reviewers reject KUnit tests and driver code for hardware variants not supported by DT or matched compatibles. |
| **ISSUE-10** | Binding naming and maintainer consent | DT naming conventions (`compatible` must match filename), MAINTAINERS policy | Schema file named `sun55i-rproc.yaml` instead of `sun55i-a523-rproc.yaml`; unconfirmed maintainers listed without consent. |

---

## 2. Prior Maintainer Feedback (v1 / v2)

| ID | Issue | Target | Upstream Rule / Resolution |
|:---|:---|:---|:---|
| **MBOX-M2** | `last_tx_done()` capacity vs drain | `sun55i-msgbox.c` | Must return `true` on `count == 0` (drain), not `count < FIFO_MAX` (capacity). |
| **MBOX-M4** | Multi-IRQ ISR concurrency race | `sun55i-msgbox.c` | MMIO status read and FIFO drain must be enclosed in `spin_lock_irqsave(&mbox->lock)`. |
| **TEST-K2** | Big-Endian MMIO mock accessors | `sunxi_rproc_test.c` | Raw array indexing fails on BE; assertions must use `readl()` accessors. |
| **DT-D1** | Positional `reg-names` items | `allwinner,sun55i-a523-rproc.yaml` | Fixed positional `items: [ - const: cfg, ... ]` required; `enum` across items forbidden. |
| **DT-D2** | Positional `interrupts` items | `allwinner,sun55i-a523-msgbox.yaml` | `interrupts` and `interrupt-names` must be positional with `minItems: 1`. |
| **DISPATCH** | Threading under previous revisions | Mailing list dispatch | Do NOT use `--in-reply-to` for new major series versions; send fresh top-level threads. |

---

## 3. Scoring Criteria for Review Agents

A review run is scored as follows:
- **Recall Score**: $\frac{\text{Known Issues Found}}{\text{Total Known Issues (10)}} \times 100\%$
- **Citation Quality**: +1 point for each finding that cites the specific Linux kernel core source file (`drivers/remoteproc/remoteproc_core.c`, `kernel/irq/manage.c`, etc.).
- **False Positive Penalty**: -1 point for each invalid or contradicted finding (e.g. demanding `rproc_del()` be called last, demanding spinlocks around `disable_irq`).
