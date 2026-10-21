# v1 / v2 Review Feedback → v3 Reconciliation (kseries stage 2)

Every comment from the v1 and v2 reviews, checked against the **v3 series itself**
(`linux-cubie` branch `next-sun55i-rproc-msgbox`, 7 patches on next-20261020 since 2026-10-20,
driver/test/binding files byte-identical to the board-tested `cubie-linux-7.1`).
Re-verified 2026-10-15; the earlier version of this file cited pre-rewrite commit hashes
and described code that no longer exists (S2.1, S2.3, R1), and marked KK-4 done when the
subjects still said "schema".

v3 patches: **1** mailbox binding, **2** mailbox driver, **3** rproc binding, **4** rproc
driver, **5** DTS, **6** mailbox KUnit, **7** rproc KUnit.

Status: **FIXED** (in the code), **EXPLAINED** (answered, no code change), **OPEN**.

## Maintainers

| ID | Reviewer | Comment | v3 | Status | Evidence in v3 |
|:---|:---|:---|:--:|:--:|:---|
| KK-1 | Krzysztof (v2) | Do not thread new versions to older threads | all | FIXED | Sent as a new thread, no `--in-reply-to` (DESIGN_DECISIONS: series procedure step 6) |
| KK-2 | Krzysztof (v2 1/7) | interrupts must be constrained; list them with minItems, no free text | 1 | FIXED | `interrupts`/`interrupt-names`: four positional items, `minItems: 1` |
| KK-3 | Krzysztof (v2 4/7) | reg-names `enum` ignored his v1 feedback | 3 | FIXED | `reg`/`reg-names`: fixed list of 4 `const` items, all required |
| KK-4 | Krzysztof (v1) | No "schema"/"binding" in subjects | 1, 3 | FIXED | "dt-bindings: mailbox: Add Allwinner A523 message box", "dt-bindings: remoteproc: Add Allwinner A523 RISC-V remote processor" |
| KK-5 | Krzysztof (v1) | No `$ref` type on standard properties | 3 | FIXED | `firmware-name: maxItems: 1` |
| KK-6 | Krzysztof (v1) | Clock/reset counts must not be flexible | 3 | FIXED | exactly 3 clocks, 4 resets, no `minItems` |
| KK-7 | Krzysztof (v1) | Describe hardware, not driver behaviour | 1, 3 | FIXED | no driver concepts in descriptions (one open wording nit, see N-1) |
| KK-8 | Krzysztof (v1) | memory-region items must be listed | 3 | FIXED | positional `memory-region` + `-names` (D10) |
| CY-1 | Chen-Yu (v1) | SoC-specific compatibles | 1, 3 | FIXED | `allwinner,sun55i-a523-msgbox`, `allwinner,sun55i-a523-rproc` |
| CY-2 | Chen-Yu (v1) | Confirm the host bank IRQ | 1, 5 | FIXED | GIC SPI 0, board-verified |
| CY-3 | Chen-Yu (v1) | Send to the right people | all | FIXED | TODO §5.6a T3 list, `--suppress-cc=all` |
| RH-1 | Rob (v1) | Example headers must resolve, examples must build | 1, 3 | FIXED | `dt_binding_check` clean on next-20261015 |

## Sashiko (v2)

| ID | Patch (v2) | Finding | v3 | Status | Evidence in v3 |
|:---|:---|:---|:--:|:--:|:---|
| SB-1 | 1/7 | interrupt-names not constrained | 1 | FIXED | positional `const` list |
| SB-2 | 1/7 | A527 compatible missing | 1 | EXPLAINED | A523/A527/T527 are one die; one compatible (CY-1) |
| SB-3 | 1/7 | reg size in description vs example | 1 | FIXED | sizes no longer in descriptions |
| SB-4 | 2/7 | lockless multi-IRQ handling | 2 | FIXED | only the host bank IRQ; handler under `mbox->lock` |
| SB-5 | 2/7 | `last_tx_done` `< FIFO_MAX` | 2 | EXPLAINED | Kept `count < 8` on purpose (DESIGN_DECISIONS "Message box TX completion", 2026-10-16): a kick is a doorbell, the data is in shared memory, so a kick is done once the FIFO took it and has room; `send_data()` refuses a full FIFO. `count == 0` made every queued kick wait for the 1 ms poll. Must be said in patch 2's v3 notes |
| SB-6 | 2/7 | OOB write from unbounded DT `irq_cnt` | 2 | FIXED | code removed: only `platform_get_irq(pdev, 0)` |
| SB-7 | 2/7 | NULL deref in IRQ during teardown | 2 | FIXED | devm order: controller unregistered first, its channel shutdown clears the IRQ enable bits; probe clears all enables; handler only acts on enabled & pending channels |
| SB-8 | 3/7 | big-endian mock MMIO | 6 | FIXED | register access goes through KUnit static stubs to a mock in plain C; no `readl()` on RAM, no arch restriction (28/28 under UML) |
| SB-9 | 3/7 | missing `SUN55I_MSGBOX` dependency | 6 | FIXED | `depends on SUN55I_MSGBOX && KUNIT` |
| SB-10 | 3/7 | startup flush test passes via the failsafe | 6 | FIXED | mock FIFO pops on read; `test_functional_startup_flushes_stale_fifo` checks the FIFO is empty and the pending bit cleared (fails if the flush is removed) |
| SB-11 | 4/7 | r_sram/r_sram1 not required | 3 | FIXED | all 4 reg entries required |
| SB-12 | 5/7 | unbalanced `disable_irq` (crash IRQ) | 4 | FIXED | atomic `SUNXI_RPROC_CRASH_IRQ_ARMED` bit |
| SB-13 | 5/7 | `kick_msg` race + immediate txdone | 4 | FIXED | static per-vring payloads, `knows_txdone = false` |
| SB-14 | 5/7 | double mapping, conflicting attributes | 4 | FIXED | carveouts `MEMREMAP_WC`, SRAM pools reuse the SRAM mapping |
| SB-15 | 5/7 | core runs early, broken reset fallback | 4 | FIXED | all resets required, no fallback |
| SB-16 | 5/7 | virtqueue UAF from late mailbox IRQ | 4 | FIXED | `rproc_del()` → `mbox_free_channel()` → `cancel_work_sync()` |
| SB-17 | 5/7 | priv UAF in probe error path (workqueue) | 4 | FIXED | devm workqueue after devm rproc; error path frees the channel and cancels the work |
| SB-18 | 5/7 | rproc UAF in remove (crash IRQ race) | 4 | FIXED | devm IRQ freed before the devm rproc; atomic arm bit |
| SB-19 | 5/7 | unmatched ATT addresses used as host PAs | 4 | FIXED | `da_to_va` returns NULL outside the two SRAM windows |
| SB-20 | 5/7 | crash IRQ state leaked on start failure | 4 | FIXED | IRQ armed only after the core reset is released |
| SB-21 | 5/7 | boot address write not flushed | 4 | FIXED | `readl()` after the write, before the reset is released |
| SB-22 | 6/7 | big-endian mock MMIO | 7 | FIXED | `readl()` |
| SB-23 | 6/7 | false-positive `kick_msg` test | 7 | FIXED | `kick_msg` gone; `test_kick_payload_outlives_call` queues a kick behind another and checks the payload sent later |
| SB-24 | 6/7 | missing `SUNXI_REMOTEPROC` dependency | 7 | FIXED | `depends on SUNXI_REMOTEPROC && KUNIT && (ARM64 \|\| COMPILE_TEST) && !UML` |

## Own pre-send review (TODO.md §5.6a D1–D16)

| ID | Item | Status |
|:---|:---|:--:|
| D1–D12, D14–D16 | see TODO.md §5.6a; all in the v3 code. Board 2026-10-20: KUnit 39/39 (msgbox 24, rproc 15), full sweep all profiles PASS | FIXED / DECIDED |
| D13 | msgbox clears the pending bit before draining (vendor: after) | FIXED (drain then clear; board: 1 IRQ per message; KUnit `test_irq_clears_pending_after_drain`) |

## Open before sending

| ID | Item |
|:---|:---|
| ~~N-1~~ | done: `mboxes` described as hardware (7.1 `82b64e947b5b`, in v3 patch 3) |
| ~~D13~~ | done 2026-10-17 |
| ~~T-DRAM~~ | not tested and not asked for by reviewers; cover letter now claims only SRAM-loaded firmware (DRAM carveout lookup covered by KUnit) |
| ~~T-RT~~ | done 2026-10-16: non-RT PREEMPT + DEBUG_ATOMIC_SLEEP + PROVE_LOCKING, all tests pass, no splats from these drivers (one harmless debug warning in the rpmsg core at TX-buffer exhaustion, not reported: TODO R-RPMSG) |
| SOB | owner reviews the series and adds his Signed-off-by (no AI sign-off) |
