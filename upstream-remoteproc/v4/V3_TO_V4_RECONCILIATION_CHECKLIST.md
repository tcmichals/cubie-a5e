# v3 Review Feedback → v4 Reconciliation

Every comment from the v3 review ([../v3/COMMENTS.md](../v3/COMMENTS.md), 6 emails), with
the v4 change and how it is verified. Status: **DECIDED** (owner chose), **PLANNED** (fix
follows from a decision or needs none), **OPEN** (owner yes/no needed), **FIXED** (in the v4
code, with evidence), **EXPLAINED** (answered, no code change; only with the owner's OK).

v4 patches: **1** mailbox binding, **2** mailbox driver, **3** rproc binding, **4** rproc
driver, **5** DTS, **6** mailbox KUnit, **7** rproc KUnit.

## Open for the owner

| Q | Question | Affects |
|:--|:--|:--|
| ~~Q1~~ | **Answered 2026-11-03: yes, remove** (no interrupt line). Was: Remove the crash interrupt from the driver and the binding? No line exists on the A523, the vendor BSP has none, it was never tested on hardware; the crash dump stays. A mailbox crash message can come later | V3-S6, V3-K2c; patches 3, 4, 7 |
| ~~Q2~~ | **Answered 2026-11-03: optional** (keep `minItems: 1`, give the firmware reason in the binding and reply). Was: Are the `dram` and `trace` memory regions always used? Yes → fixed list, no `minItems`. No → the binding gives a hardware reason for each optional entry | V3-K2a, V3-K2b; patches 3, 4, 5 |
| Q3 | Fix SRAM resource-table addresses to E906 DAs with `dma-ranges` (TC-3, HARSH rproc M2, never decided)? No → the cover letter says they are host PAs | patches 3, 4, firmware |
| ~~Q4~~ | **Closed 2026-11-03, no change**: `mboxes` stays optional, as in `fsl,imx-rproc`, `st,stm32-rproc`, `ti,k3-m4f-rproc` (only `ti,k3-dsp-rproc` requires it); Krzysztof did not comment on it. Was: keep `mboxes` optional for UIO (TC-7)? | patch 3 |

Triple check of v1/v2/v3: [review/TRIPLE_CHECK_20261103.md](review/TRIPLE_CHECK_20261103.md) (TC-1 … TC-10).

## Maintainers

| ID | Reviewer | Patch | Comment | Repeat | v4 change | Status | Evidence in v4 |
|:---|:---|:--:|:---|:--:|:---|:--:|:---|
| V3-K1 | Krzysztof | 1 | "Sashiko comment looks valid, rest seems ok" (`allwinner,tx-done-irq` is policy) | – | Drop the property; TX done always by the receiver bank's write interrupt; delete the poll/timer mode and `tx_held` path | DECIDED | |
| V3-K2a | Krzysztof | 3 | `memory-region` `minItems: 1`: "same comments as for v2" | v2 | Keep `minItems: 1`; binding description says why `dram`/`trace` are optional (Q2) | DECIDED | |
| V3-K2b | Krzysztof | 3 | `memory-region-names` `minItems: 1` | v2 | Same as V3-K2a | DECIDED | |
| V3-K2c | Krzysztof | 3 | `interrupts` (crash): "same comments as for v2" | v2 | Drop `interrupts` (Q1 = yes) | DECIDED | |

## Sashiko

| ID | Patch | Sev | Finding | Repeat | v4 change | Status | Evidence in v4 |
|:---|:--:|:--:|:---|:--:|:---|:--:|:---|
| V3-S1 | 1 | Med | `allwinner,tx-done-irq` encodes driver policy | – | Same as V3-K1 | DECIDED | |
| V3-S2 | 2 | High | `send_data()` returns `-EBUSY` for a full FIFO in IRQ mode: message stays in `chan->msg_data[]` with `active_req == NULL` → UAF after a blocking timeout, second `tx_prepare()`, premature txdone | – | Hold the message and return 0; the TX IRQ handler writes the held message once a slot is free, then reports txdone | PLANNED | KUnit: blocking timeout then late IRQ; concurrent send while held |
| V3-S3 | 2 | Med | Lock dropped between the FIFO write and clearing/arming the latched write status → stale pending bit completes a message while the FIFO is full | – | Clear status and arm in the same `mbox->lock` section as the FIFO write | PLANNED | KUnit: latched status + fill to 8 |
| V3-S4 | 2 | High | `shutdown()` with the line shared (users > 1): `synchronize_irq()` does not stop a re-armed level IRQ firing again before the final disarm, racing `mbox_free_channel()` | – | Per-channel "shutting down" flag set under `mbox->lock`; `send_data()` never arms once it is set | PLANNED | KUnit: second IRQ after `synchronize_irq()` |
| V3-S5 | 4 | High | `sunxi_rproc_vq_work()` walks `rproc->notifyids` without `rproc->lock` → UAF against `rproc_resource_cleanup()`. Closed in v3 on a wrong precedent (TC-2: imx/xlnx free the mailbox in `stop()`) | v2 | Take `rproc->lock` and check `RPROC_RUNNING`, as `stm32_rproc_mb_vq_work()` and `imx_dsp_rproc_vq_work()` do. No `cancel_work_sync()` in `stop()`: `stop()` runs under `rproc->lock` and the work takes it → deadlock | PLANNED | KUnit: work after stop is a no-op |
| V3-S6 | 4 | High | `stop()` skips `synchronize_irq()` when the crash handler already cleared ARMED → unbalanced `enable_irq()`, crash reported after restart | v2 | Crash interrupt removed (Q1 = yes) | DECIDED | |
| V3-S7 | 7 | Med (**build break**, TC-1) | `SUNXI_REMOTEPROC_KUNIT_TEST` needs `depends on PM` (`power.usage_count`, PM-domain failure test) | – | Add `PM` to `depends on` | PLANNED | PM=n build (PRESEND_CHECKLIST) |
| V3-S8 | 7 | Low | `use_mmio_mocks()` `__iomem` casts lack `__force` | – | Add `__force` | PLANNED | `C=1` sparse clean (PRESEND_CHECKLIST) |

## Why these came back

Our own v3 reviews found 4 of the 9 (HARSH H1, M1, M2, part of M4) and they were closed on a
one-sided precedent, recorded "left as is" without asking the owner, or kept because a design
decision was already on record. A wrong rule (`minItems: 1`) stayed in our docs after being
flagged. Truly missed: the mailbox-core races, PM=n, sparse. The fixes are in
[PRESEND_CHECKLIST.md](PRESEND_CHECKLIST.md) §3.

## Docs corrected (2026-11-03)

| Doc | Change | Status |
|:--|:--|:--:|
| `../UPSTREAM_LESSONS_LEARNED.md` | Lesson 1.5 pattern without `minItems`; new §6 (Lessons 6.1–6.5); checklist: no `minItems` without a reason, v4 mailbox TX, sparse, PM=n, concurrency pass, re-check, no silent closing | DONE |
| `../../.agents/rules/upstream-patch-submissions.md` | Rules 2a (no `minItems` without a hardware reason), 2b (no driver-policy properties); "a disagreeing review is not closed by itself either" | DONE |
| `../v3/review/REVIEW_ANSWER_KEY.md` | DT-D2 corrected, DT-D3 added | DONE |
| `../DESIGN_DECISIONS.md` | Record "TX done by write interrupt only" (V3-K1) and the answers to Q1/Q2 | TODO (the other PC has an uncommitted entry: merge, don't duplicate) |
| `../../TODO.md` §5.6a | One row per V3 item (V4-*); V4-VQWORK must not say "cancel the work on stop" | TODO (same: rows exist uncommitted on the other PC) |
