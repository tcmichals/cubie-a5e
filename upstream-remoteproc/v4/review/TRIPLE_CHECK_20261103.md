# v1 / v2 / v3 triple check, 2026-11-03

Owner: "review V1/V2/V3 and see what needs to be fixed, I want this triple checked".

**Baseline.** v3 was sent from `linux-cubie` worktree `linux-v3-next-20261027`, branch
`next-sun55i-rproc-msgbox` @ `b47352fa3f97` on next-20261030 (`7649ba73f9e7`). The 8 new files
rebuilt from the sent `v3/*.patch` are byte-identical to that branch **and** to
`cubie-linux-7.1`, so one code base was checked. v4 fixes go on 7.1 first, then fold onto a fresh
next branch (the established flow).

## Method

| Pass | What | How |
|:--|:--|:--|
| 1 | Every comment in all 25 review emails (v1: 11, v2: 8, v3: 6) | Parsed each `.eml`; Krzysztof's v1 mails re-read with their quoted context, because his v3 "same comments as for v2" points through v2 ("you ignored my feedback") back to v1 |
| 2 | Each finding against the sent v3 code | Re-ran the failure scenario on the code (Lesson 6.2), not "code touching it exists" |
| 3 | Independent checks | `CONFIG_PM=n` W=1 build on the next tree; peer drivers in next-20261030 for every precedent we cited; `get_maintainer.pl` on the sent patches; an audit of how our own reviews (CLAUDE, GEMINI, INDEPENDENT, HARSH, FINAL, FINAL_REVIEW_20261029) closed their findings |

## 1. v1 and v2 findings in the v3 code

All v1/v2 Sashiko, Krzysztof, Chen-Yu and Rob items are fixed in v3 **except these, which came
back in v3**:

| v1/v2 item | v3 state | Back as |
|:--|:--|:--|
| v1 KK (msgbox 1/7): "How interrupt could be optional? It either is or is not there." | Applied to the msgbox, **not** to the rproc crash interrupt (optional, no hardware line) | V3-K2c |
| v1 KK (rproc 4/7): resets/names "cannot be flexible … same comments everywhere else"; memory-region "List the items instead" | Items listed, but `minItems: 1` kept them flexible | V3-K2a/b |
| v2 Sashiko 5/7: UAF of virtqueues due to late mailbox interrupts | Teardown order fixed; the `vq_work` walk during stop/cleanup was not | V3-S5 |
| v2 Sashiko 5/7: unbalanced `disable_irq` via crash-IRQ race | Atomic bit added; `stop()` still skips `synchronize_irq()` when the handler cleared it | V3-S6 |

Checked and fine in v3 (scenario re-run): v1 msgbox reset leak (explained: shared reset never
asserted, as sun6i-msgbox), ignored `devm_request_irq` failure, unclocked MMIO at remove (devm
order), TOCTOU clear (drain then clear; hardware ignores the clear while not empty), unbounded
drain; v2 OOB `irq_cnt` (code gone), lockless multi-IRQ (one handler, `mbox->lock`), NULL deref
at teardown (`synchronize_irq()` in `shutdown()`), `last_tx_done` (moot in v4: poll mode
removed); v1 rproc da_to_va overflow, WC/WB alias, deferred-probe ERR_PTR, Space 1 bounds, STA_ADD
under reset (recovery does not call unprepare), teardown order, stack kick payload; v2 kick race,
double mapping, reset fallback, probe-error UAF, remove UAF (devm order), unmatched ATT,
start-failure IRQ state, boot-address flush; all v1/v2 binding and KUnit items; Chen-Yu's one
compatible and full-series recipients; Rob's example headers; Krzysztof's get_maintainer Cc lines,
`|`, redundant descriptions, `status`, subject wording, threading.

## 2. New from the triple check

| ID | Sev | Finding | Evidence | v4 |
|:--|:--:|:--|:--|:--|
| TC-1 | High | **V3-S7 is a build break, not a Medium.** `CONFIG_PM=n` (allowed: ARCH_SUNXI does not select PM) fails to compile `sunxi_rproc_kunit.c`: `struct dev_pm_info has no member named usage_count` (lines 962, 977, 1107). Both drivers themselves build clean at W=1 with PM=n. randconfig / the kernel test robot would report it | build log, next tree, `kobj-v4-pmn` | `depends on PM` (V4-KPM) |
| TC-2 | High (process) | **The precedent that closed V3-S5 was wrong.** HARSH-1 (TODO) left the unlocked `idr_for_each()` "same as `imx_rproc.c`, `xlnx_r5_remoteproc.c`". Both of those free the mailbox in `stop()` (`imx_rproc.c:526,730`; `xlnx_r5_remoteproc.c:334-369`), so no receive work runs during cleanup. We keep the mailbox across `stop()`, which is the stm32 / imx_dsp case, and both lock (`stm32_rproc.c:252-266`, `imx_dsp_rproc.c:497-511`). FINAL_REVIEW_20261029 Tier 5 then wrote "walks `notifyids` like imx/stm32"; FINAL-1 wrote "imx/stm32 too". All three citations were one-sided | peer code in next-20261030 | V4-VQWORK as planned |
| TC-3 | Medium | **HARSH remoteproc M2 was never decided.** With the vring pool on SRAM1, the `vram` carveout `da`, every `FW_RSC_ADDR_ANY` vring `da` and every rpmsg buffer address are host PAs (`0x072c....`), not E906 DAs (`0x4000....`); the firmware converts by hand (`firmware/common/hal/rpmsg.cpp`). The resource-table ABI says `da` is the remote's view. Proposed fix in the review: `dma-ranges` on the rproc node, SRAM carveouts registered with the E906 DA, drop ATT rows 3-4. No TODO row, no DESIGN_DECISIONS entry, not in the cover letter | HARSH_REVIEW_20261027_remoteproc.md M2; grep of TODO/DESIGN_DECISIONS | **Owner Q3** |
| TC-4 | Medium | **FINAL_REVIEW_20261029 passed v3 as CLEAN with three wrong tiers**: 2.4 ("`msg_submit()` keeps the message on `-EBUSY` … resubmits it": never traced the blocking timeout → V3-S2), 5 (TC-2), 6 ("recorded decisions, not findings": `tx-done-irq` → V3-K1/S1) | the review file | `/kreview` updated 2026-11-03 |
| TC-5 | Medium | **Chen-Yu was sent v3 at the wrong address.** v3 went to `wens@csie.org`; MAINTAINERS (3 entries) and his v1 review use `wens@kernel.org` | `get_maintainer.pl` on the v3 patches; bash history | v4 recipients: `wens@kernel.org` |
| TC-6 | Low | Crash with recovery disabled, then unbind: `rproc_shutdown()` refuses a CRASHED rproc, so `unprepare()` never runs and clocks, resets and the PM reference stay on (FINAL-1 "not driver bugs"). Only reachable through the crash interrupt | `remoteproc_core.c` `rproc_shutdown()` | Goes away with Q1 = remove; otherwise needs a fix |
| TC-7 | Low (owner decision exists) | `mboxes` is optional because "a user can drive the message box from userspace (UIO)". That is a software reason for an optional property, the same class Krzysztof rejected for `tx-done-irq` and `minItems`. He did not comment on it in v3 | rproc binding v3; DESIGN_DECISIONS "Mailbox is optional" | **Owner Q4** (yes/no: keep) |
| TC-8 | Info | next has `rproc_elf_load_rsc_table_optional()` (stm32 and imx use it). Same semantics as our open-coded `-EINVAL` check, but it `return`s from the caller, so it cannot be used before our carveouts are registered. Mention in the 4/7 changelog if a reviewer asks | `remoteproc_internal.h:168` | none |
| TC-9 | Info | Removing the poll mode (V4-TXIRQ) removes or rewrites about 7 msgbox KUnit cases (`last_tx_done` sweep, held-for-full, shutdown-drops-held, keeps-order, held-follows, dropped-after-timeout); the 37/26 counts in the cover letter change | `sun55i_msgbox_kunit.c` | rewrite for IRQ mode |
| TC-10 | Gap | sparse (`C=1`) still not run: sparse is not installed on this PC and building it from a fresh clone was not permitted. V3-S8 is fixed by reading, not by a tool | — | owner: `sudo apt install sparse` |

`get_maintainer.pl` also lists riscv, PREEMPT_RT and reset-controller people by keyword; they
stay off the list (TODO T3 decision). Everyone else matches the v3 recipients.

## 3. Owner questions (all yes/no)

| Q | Question |
|:--|:--|
| Q1 | Remove the crash interrupt from driver and binding? (Krzysztof v1: "It either is or is not there"; no line exists. Also removes V3-S6 and TC-6.) |
| Q2 | Are `dram` and `trace` always used? Yes → fixed list of three, no `minItems`. |
| Q3 | Fix SRAM addresses to E906 DAs with `dma-ranges` (TC-3) in v4? No → the cover letter must say SRAM resource-table addresses are host PAs. |
| Q4 | Keep `mboxes` optional for UIO (TC-7)? |

## 4. Why our reviews missed these (process, see UPSTREAM_LESSONS_LEARNED §6)

Our reviewers found most of it (HARSH H1, M1, M2, rproc M1, M2). It was lost after the review:
closed on precedents that cited only the agreeing peers (TC-2), filed as "recorded decisions,
not findings" (TC-4), or never written down at all (TC-3). The checks that would have caught the
rest were not in the pipeline (TC-1 PM=n, TC-10 sparse). Rules, `/kreview` and `/kseries` were
corrected on 2026-11-03; TC-2 and TC-3 are the proof that the old rule ("a disagreeing review does
not change the code by itself") was being used to close findings rather than to ask.
