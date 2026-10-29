# v3 pre-send review rerun, 2026-10-29

Why: the 10-27 reviews (`FINAL_REVIEW_20261027_*`, `HARSH_REVIEW_20261027_*`) predate
`eea917a5d5fb` (SRAM names, msgbox copyright), the msgbox KUnit TX-done changes
(`b19a950e320c`, `97f6e462ba87`, `3b8f885f2f4d`), the remap wording (`8671beca6667`), the
rebase onto next-20261027 and the rewritten commit messages.

Target: linux-cubie `v3-next-20261027` @ `6c779567f9f9`, base next-20261027 (`151a134a4e6e`).
Full read of the current files, not only the deltas: `sun55i-msgbox.{c,h}`,
`sunxi_rproc.{c,h}`, both bindings, the DTS hunk, the KUnit `fire_tx_irq()`, the cover letter.

## /kreview

| Tier | Result | Notes |
|:--|:--|:--|
| 1 DT schema | PASS | positional `items` for reg/clocks/resets/interrupts and names, `additionalProperties: false`, no driver prose, examples match. Fixed: a short broken line in the rproc description (`0a77050147bd`) |
| 2 C invariants | PASS | `count < FIFO_MAX` done; a full FIFO is never written (held in timer mode, `-EBUSY` + write IRQ in tx-done-irq mode); write IRQ masked before ack; drain then clear; drain capped at 8; client callbacks (`mbox_chan_received_data()`, `mbox_chan_txdone()`) outside `mbox->lock`; rx callback only queues work; STA_ADD and remap writes read back before the reset is released |
| 2.4 core contract | PASS | checked against next-20261027 `mailbox.c`: `msg_submit()` keeps the message on `-EBUSY` and `tx_tick()` from the write IRQ resubmits it (`active_req == MBOX_NO_MSG`); poll timer starts only after a successful send, so timer mode holds the message and returns 0; a blocking send that timed out leaves a held bit that the next `send_data()` clears; startup failure goes to `mbox_clean_and_put_channel()` without `shutdown()` |
| 3 unwinding | PASS | `prepare()` unwinds in reverse; `rproc_fw_boot()` sends a `parse_fw()` failure to `unprepare_rproc` without `rproc_resource_cleanup()`, and `parse_fw()` frees its own entries; remove: `rproc_del()` then `mbox_free_channel()` then `cancel_work_sync()` |
| 4 lifecycle races | PASS | crash IRQ armed bit shared by `start()`/`stop()`/handler; `disable_irq()` in `stop()` holds no lock; shutdown syncs the rx handler before the core clears `chan->cl`, and disarms the write IRQ again after the TX-done handler finished |
| 5 peers | PASS | vring work walks `notifyids` like imx/stm32; same `rproc_vq_interrupt()` from work as stm32; reset never asserted as in sun6i-msgbox |
| 6 false positives | — | D8 (KUnit stubs in the MMIO path), D9 (remap bit 1), `parse_fw` ignoring `-EINVAL` (stm32) are recorded decisions, not findings |

Lock order: `chan->lock` → `mbox->lock` (send path); rx, tx-done and poll paths take
`mbox->lock` alone and drop it before calling the core. Lockdep runs (RT and PREEMPT, with
PROVE_RAW_LOCK_NESTING on PREEMPT) agree.

KUnit `fire_tx_irq()`: under forced threading `request_irq()` moves the handler to
`action->thread_fn`; the test calls it with BH (and on !RT IRQs) off, as
`irq_forced_thread_fn()` does. `irq_get_irq_data()` is exported; `struct irq_desc` is public in
`<linux/irqdesc.h>`.

Verdict: **CLEAN** (no code change needed).

## /kseries

1. Lore: v2 thread re-fetched, 35 messages; nothing after Krzysztof's 2026-10-01 mails. All
   19 review mails were already in `V1_V2_TO_V3_RECONCILIATION_CHECKLIST.md` /
   `V1_V2_VERIFICATION_20261027.md` (the 11 v1 copies the fetch wrote into `v2/emails/` were
   removed).
2. Resolution gate: no UNRESOLVED item.
3. Range-diff against the export the 10-27 reviews read (`fe62c9e`): changes are the
   commits listed above plus commit messages; no unrelated files. Range-diff of the refold:
   only patch 3 changed (the reflow).
4. Replies: `REPLIES_DRAFT.md` unchanged (no new mail).
5. Export: changelogs below `---` in all 7 patches; author Tim Michals on all; no AI
   trailers; no `--in-reply-to`; `base-commit` set. checkpatch --strict, now with the SPDX
   check working (`ply` was missing on this PC, so `spdxcheck.py` had not run before; venv at
   `~/projects/cubie/.venv-kernel-tools`): only "Missing Signed-off-by" (the owner adds it) and
   "does MAINTAINERS need updating?" (entries in patches 2 and 4 cover `sun55i*` and
   `sunxi_rproc*`). `dt_binding_check` on the rproc binding after the reflow: clean.

Cover letter: the testing section now also lists PREEMPT, modules, clang and
PROVE_RAW_LOCK_NESTING, which were tested on 10-28.

Left for the owner: add Signed-off-by, rebase on the send day's linux-next, send.
