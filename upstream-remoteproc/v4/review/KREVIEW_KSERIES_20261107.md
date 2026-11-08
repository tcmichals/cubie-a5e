# v4 /kreview + /kseries (2026-11-07)

Series: local branch `v4-next-20261105` in `linux-v4-next`, tip `3eedd6a66aea`, base
next-20261105. Exported to the scratchpad with `format-patch -v4 --cover-letter --base`, not
into `v4/` (that is done at send time).

| # | Commit | Subject |
|:--|:--|:--|
| 1 | `4e541f01761c` | dt-bindings: mailbox: Add Allwinner A523 message box |
| 2 | `14439612e199` | mailbox: sun55i: Add Allwinner A523 message box driver |
| 3 | `3f8b57d0d4bd` | dt-bindings: remoteproc: Add Allwinner A523 RISC-V remote processor |
| 4 | `eca82dfe426b` | remoteproc: sunxi: Add Allwinner A523 XuanTie E906 driver |
| 5 | `6a3700d6d4ba` | arm64: dts: allwinner: a523: Add message box and remoteproc nodes |
| 6 | `8cd7c789a400` | mailbox: sun55i: Add KUnit tests |
| 7 | `3eedd6a66aea` | remoteproc: sunxi: Add KUnit tests |

All authored by Tim Michals <tcmichals@gmail.com>; no Signed-off-by yet (owner), no AI trailers.

## /kreview tiers

| Tier | Result | Notes |
|:--|:--:|:--|
| 1 DT bindings | **FAIL** (K-1) | msgbox binding clean (fixed lists, no `minItems`, no policy property). rproc: `memory-region` `minItems: 1` with a firmware reason only (rule 1.6), and the description promises combinations the schema rejects |
| 2 Concurrency / HW invariants | PASS | Full FIFO never written; TX status cleared and armed under `mbox->lock` with the FIFO write; level write IRQ masked before ack; no client call under the lock; `send_data()` returns 0 or `-ESHUTDOWN` (only after `shutdown()` started); lock order `chan->lock` → `mbox->lock`, never reversed; vq work deferred to an ordered workqueue |
| 3 Error unwinding / lifetimes | PASS | `prepare()` gotos in reverse order; probe all devm; remove `rproc_del()` → `mbox_free_channel()` → `cancel_work_sync()`; vq work takes `rproc->lock` + `RPROC_RUNNING`, no cancel in `stop()`. Core `rproc_add()` double put is CORE-RPROC-ADD (separate patch) |
| 4 Lifecycle races / earlier findings | PASS | Concurrency pass (C1, C2 Low, owner yes/no on mentioning). v3 items re-run: see /kseries Stage 2 |
| 5 Peers | PASS with note | Mailbox: sun6i-msgbox (shared reset never asserted), arm_mhu. Remoteproc: stm32/imx_dsp for the locked vq work. Precedent for K-1 below lists every peer |
| 6 False-positive filter | done | K-1 is reported as CONFLICTS WITH DECISION Q2, not dropped |

### K-1 (Medium) rproc binding `memory-region`: CONFLICTS WITH DECISION Q2

1. **The description and the schema disagree.** The items are positional (`vram`, `dram`,
   `trace`) with `minItems: 1`, so only `[vram]`, `[vram, dram]`, `[vram, dram, trace]` validate.
   The text says "Firmware that uses virtio needs the vring and buffer pool" (so firmware without
   virtio would omit `vram`: rejected by the schema, and the driver fails the probe with `-EINVAL`
   because `memory-region[0]` must be a pool, `of_reserved_mem_device_init_by_idx()`) and that
   SRAM-only firmware needs no DRAM region (so SRAM-only firmware with a trace buffer is
   `[vram, trace]`: rejected). The 3/7 changelog says "only the pool is required", but
   `memory-region` is not in `required` either. Krzysztof checks exactly this.
2. **Rule 1.6:** the reason given is the firmware, not hardware. Krzysztof pointed at these two
   `minItems` on v3 ("same comments as for v2"; v1: "List the items instead", "cannot be
   flexible").
3. **All peers** (next-20261105, `bindings/remoteproc/`): `minItems` on `memory-region` appears in
   `fsl,imx-rproc` (1-32, unnamed, no fixed meaning per index), `ti,k3-dsp-rproc` (2-8, unnamed),
   `qcom,milos-pas`, `qcom,shikra-pas`, `qcom,sm8550-pas` (each narrows the count per compatible in
   `allOf`). Fixed lists: `qcom,kaanapali-soccp-pas`, `qcom,nord-pas`, `qcom,sa8775p-pas`,
   `qcom,msm8916/8996/sc7180/sc7280-mss-pil`; single entry (`maxItems: 1`): `mtk,scp`,
   `ti,omap-remoteproc`, most `qcom,*-pas`/`-pil`; free form: `st,stm32-rproc`, `renesas,rcar-rproc`,
   `ti,k3-m4f-rproc`. None has one compatible, named positional entries and `minItems` for a firmware
   reason.

Owner question: keep Q2 as decided (then fix the description so it matches the schema: the
pool is always first and always present when `memory-region` is; dram and trace are optional in
that order) or switch to the alternative already offered in the reply draft (drop `dram`/`trace`
from the binding; the firmware requests them through its resource table, leaving `memory-region`
as `maxItems: 1`)?

### Low findings (cleanups, no behaviour change)

| ID | Patch | Finding | Fix |
|:--|:--:|:--|:--|
| L-1 | 2 | Tracker IDs in upstream comments: `(V4-HELD)` `sun55i-msgbox.c:317`, `(V4-ARM)` `:330`, `(V4-SHUT)` `:390` | drop them |
| L-2 | 2 | `sun55i-msgbox.h:65` `lock` comment still says "in timer mode the FIFO check and write together with the held message"; timer mode is gone | "the FIFO check and write with the held message" |
| L-3 | 4 | Crash IRQ removal leftovers: `#include <linux/interrupt.h>` in `sunxi_rproc.c`, `<linux/irqreturn.h>` in `sunxi_rproc.h` | drop |
| L-4 | 7 | `<linux/irq.h>`, `<linux/irqdesc.h>` in `sunxi_rproc_kunit.c`, used only by the removed crash IRQ tests | drop |
| L-5 | 4 | Commit body: ".parse_fw() registers ... carveouts ... then loads the resource table"; the code loads the table first, then adds the carveouts and segments | reword |
| L-6 | 2 | msgbox Kconfig help: "(send, peek, startup, shutdown)" left on a short line after `last_tx_done` was dropped | rewrap |
| L-7 | 7 | 7/7 changelog credits only Sashiko for `depends on PM`; the kernel test robot reported the same build error on v3 (parisc-allyesconfig, PM not available) | add "and the kernel test robot" (no tags: fixed in a new version of the same patch, as the robot asks) |
| L-8 | 0 | Cover letter: thanks Krzysztof only for v3; Uwe Kleine-König reviewed v3 2/7 and 4/7 | add Uwe (and Cc him) |
| L-9 | 0 | Cover letter "checkpatch --strict clean": it reports the MAINTAINERS note on 1, 3, 6, 7 (the entries come with 2 and 4 and cover those files) | say so |
| L-10 | 0 | Cover letter: UML "65 passed, 1 skipped" next to board "37 and 23": 60 cases, one runs 7 parameters | say "66 including the 7 parameters of one test" or count cases |
| L-11 | 0 | Cover letter fail-nth line says every error path unwinds; rproc probe injection also hit the core `rproc_add()` double put (not our code) | owner: mention it, or the separate core patch |

## /kseries

**Stage 1, ingestion.** lore thread mbox of v3 (17 messages): one not ingested before, the kernel
test robot on 7/7 (2026-11-08 13:13 +0800, `202611081239.zlQqllPm-lkp@intel.com`), saved as
`v3/emails/17_*`. Build error `struct dev_pm_info has no member named usage_count` at
`sunxi_rproc_kunit.c:962,977,1107`, parisc-allyesconfig W=1. Same as V3-S7 / TC-1. The robot's
config has no `CONFIG_PM` at all, so v4's `depends on ... && PM` turns the test off there. The
same build compiled both drivers and the msgbox KUnit on 32-bit big-endian parisc without errors.

**Stage 2, resolution.** Every V3 item has a v4 change (V3_TO_V4_RECONCILIATION_CHECKLIST.md);
new row V3-R1 (robot) = V3-S7. Re-run on the v4 code: S2, S3, S4, S5 by the KUnit cases that fail
with the fix reverted (PRESEND §2); S6/K2c gone with the interrupt (no `interrupts` in the
binding, no IRQ code in the driver); S7 by the PM=n build; S8 by sparse C=2; K1/S1 property gone
from binding and driver; U1 include gone from both. **K2a/K2b: open again (K-1).**

**Stage 3, range-diff** (v3 `b47352fa3f97` on 7649ba73f9e7 vs v4): 7 ↔ 7, same order. Deltas in
our files only: msgbox binding (property dropped), rproc binding (description added,
`interrupts` dropped), msgbox driver (TX IRQ only, held message, arm in lock, shutdown flag,
include), rproc driver (vq work lock, crash IRQ removed, include), both KUnit files, Kconfig help
and `depends on PM`. Patch 5: commit message only ("Changes in v4: None"). No unrelated churn.

**Stage 4, replies.** Owner 2026-11-07: no reply to Krzysztof's 1/7 "Sashiko comment looks
valid" (accepted; reply removed from REPLIES_DRAFT.md). The 3/7 reply stays (it explains why
`minItems` is kept) and depends on K-1.

**Stage 5, assembly.**
- Changelogs below `---` in all 7, reviewers named, repeats say "sorry I missed it in v3".
- `checkpatch --strict`: 0 errors; 1 warning on 1, 3, 6, 7 (MAINTAINERS note), 0 on 2, 4, 5.
- `get_maintainer.pl`: as v3 plus Chen-Yu at `wens@kernel.org`; the RT, RISC-V and reset people
  come by keyword and stay off (TODO T3). Add Uwe (L-8).
- `base-commit` in the cover letter; no `--in-reply-to`.
- Fold tip `3eedd6a66aea`, GCC 15.1 W=1, allnoconfig + dependencies: PM=n modules, PM=n built in,
  PM=y everything built in: exit 0, 0 warnings in our files; with PM=n the rproc KUnit is off
  (`depends on PM`), the msgbox KUnit builds.

## Verdict

**ACTION REQUIRED**: K-1 needs the owner's answer, and L-1 … L-11 are open (L-11 owner yes/no).
No High finding; nothing in the code paths changed from the board-tested build. All Low fixes
are comments, includes, commit text and cover letter: after folding them, re-run the per-patch
build and checkpatch (no board retest needed unless K-1 changes the binding).
