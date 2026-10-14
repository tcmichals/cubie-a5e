<!--
Claude (Opus 5.5) review following Part 1 of GEMINI_PRO_AUDIT_BUNDLE.md, 2026-10-13.
Reviewed ref a04b70b95ca9 (origin/next-sun55i-rproc-msgbox) against the live linux-cubie
tree, not the bundle copy, so framework citations are to the real base 5c4d4169604b
(linux-next 20260921). Parts 1-7 all used; v1/v2 raw .eml files read for S0.
-->

# Review: [PATCH v3 0/7] remoteproc: sunxi: Add Allwinner XuanTie E906 RemoteProc and Message Box support

## Verdict
NOT READY — v3 fixes nearly every v1/v2 Sashiko finding and most of Krzysztof's binding
comments, but the receive path added at the last minute (no workqueue) runs the rpmsg
receive code, which takes mutexes, from a hardirq handler under a spinlock. That is a
sleep-in-atomic bug on every non-RT kernel, and PREEMPT_RT on the test board hides it.
Required MMIO regions are treated as optional, so a mapping failure starts the core with an
unprogrammed boot vector. The remap register is described inconsistently between cover
letter, header and code. Several commit-message claims are false, and two of Krzysztof's v1
points are only partly addressed. None of this is large; it is one focused revision away.

## S0 Prior-review matrix

| ID | Reviewer | Request | Evidence (path:line) | Status |
|---|---|---|---|---|
| CY1 | Chen-Yu Tsai (v1) | Go through all Sashiko comments, including the critical one | see S-rows below | RESOLVED (except the issue in F1) |
| CY2 | Chen-Yu Tsai (v1) | One compatible for the A523/A527/T527 die | msgbox.yaml:23, rproc.yaml:23, sunxi_rproc.c:688 | RESOLVED |
| CY3 | Chen-Yu Tsai (v1) | Send the full series to all parties | SUBMISSION_TRACKER.md §4 send-email command omits Jassi, Bjorn, Mathieu, Chen-Yu, Jernej, Samuel, Conor, LAKML, LKML (v2 had them) | OPEN (process) |
| KK1 | Krzysztof (v1) | Drop get_maintainer Cc: trailers from commit messages | no `Cc:` in v3-0001..0007 | RESOLVED |
| KK2 | Krzysztof (v1) | msgbox: drop minItems on reg, `|` pipes, redundant descriptions | msgbox.yaml:25-51 | RESOLVED |
| KK3 | Krzysztof (v1, v2) | msgbox interrupts: constrained, listed with minItems, not free text | msgbox.yaml:53-67 | RESOLVED |
| KK4 | Krzysztof (v1) | Subject: drop redundant "binding" | v3-0001/0004 subjects end in "schema" | PARTIAL (F5) |
| KK5 | Krzysztof (v1) | rproc resets/clocks "cannot be flexible" | rproc.yaml:39,46,53,60 `minItems: 2` | PARTIAL (F5) |
| KK6 | Krzysztof (v1) | mboxes: drop minItems, use items | rproc.yaml mboxes items | RESOLVED |
| KK7 | Krzysztof (v1) | firmware-name: drop description | rproc.yaml firmware-name `maxItems: 1` | RESOLVED |
| KK8 | Krzysztof (v1) | memory-region(-names): list the items | rproc.yaml:79-91 | RESOLVED |
| KK9 | Krzysztof (v1) | Drop driver-behaviour text from interrupts description | rproc.yaml:93-95 | RESOLVED |
| KK10 | Krzysztof (v2) | Do not thread new versions under old threads | tracker §4 says no `--in-reply-to` | OPEN until sent |
| KK11 | Krzysztof (v2) | "You completely ignored my feedback" (rproc binding) | KK4-KK9 | PARTIAL; v3 changelog does not map his points one by one |
| RH1 | Rob Herring bot (v1) | Example fails dt_binding_check (undefined macros) | Part 6: dt_binding_check exit 0; macros exist in base sun55i-a523-mcu-ccu.h | RESOLVED |
| S1-1 | Sashiko v1 msgbox | Tautological tests copying driver macros | tests call real symbols via VISIBLE_IF_KUNIT | RESOLVED |
| S1-2 | Sashiko v1 msgbox | Shared reset leaked on error/remove | sun55i-msgbox.c:288-294 devm action | RESOLVED |
| S1-3 | Sashiko v1 msgbox | Ignored devm_request_irq failure | sun55i-msgbox.c:310-312 | RESOLVED |
| S1-4 | Sashiko v1 msgbox | Clock off before IRQ freed | devm_clk_bulk_get_all_enabled sun55i-msgbox.c:280 | RESOLVED |
| S1-5 | Sashiko v1 msgbox | TOCTOU status clear after drain | sun55i-msgbox.c:102-103 clear before drain | RESOLVED |
| S1-6 | Sashiko v1 msgbox | Unbounded drain loop in hardirq | sun55i-msgbox.c:106 capped at FIFO_MAX | RESOLVED |
| S1-7 | Sashiko v1 rproc | da+len overflow (Critical) | sunxi_rproc.c:315 | RESOLVED |
| S1-8 | Sashiko v1/v2 rproc | trace/dram mapped twice WB+WC | sunxi_rproc.c:502-517 single mapping | RESOLVED |
| S1-9 | Sashiko v1 rproc | ERR_PTR rx_chan freed on probe defer | sunxi_rproc.c:633,641 NULLed | RESOLVED |
| S1-10 | Sashiko v1 rproc | Space 1 caught by Space 0 bounds | ATT table sunxi_rproc.c:33-45 | RESOLVED |
| S1-11 | Sashiko v1 rproc | Recovery writes STA_ADD with cfg in reset | cfg reset deasserted in prepare sunxi_rproc.c:104 | RESOLVED |
| S1-12 | Sashiko v1 rproc | Stack variable passed to mbox_send_message | static const sunxi_rproc.c:263 | RESOLVED |
| S1-13 | Sashiko v1 dts | Node sort order, multi-string compatible | sun55i-a523.dtsi:608, 1171 | RESOLVED |
| S2-1 | Sashiko v2 msgbox | OOB write from unbounded irq_cnt | only index 0, sun55i-msgbox.c:306 | RESOLVED |
| S2-2 | Sashiko v2 msgbox | last_tx_done on "not full" | sun55i-msgbox.c:218-219 `count == 0` | RESOLVED (see F6 for cost) |
| S2-3 | Sashiko v2 msgbox | NULL cl deref racing teardown | ISR holds mbox->lock (77-120); shutdown clears enable under it (188-192) before mailbox.c:520 clears cl | RESOLVED |
| S2-4 | Sashiko v2 msgbox | Lockless multi-IRQ handler | one IRQ + lock | RESOLVED |
| S2-5 | Sashiko v2 rproc | crash_irq_enabled races / double disable | atomic bit sunxi_rproc.c:72,231,250 | RESOLVED |
| S2-6 | Sashiko v2 rproc | kick_msg race and immediate txdone | sunxi_rproc.c:263-278 | RESOLVED |
| S2-7 | Sashiko v2 rproc | Reset fallback starts core early | rst_core mandatory sunxi_rproc.c:590 | RESOLVED |
| S2-8 | Sashiko v2 rproc | Late mailbox IRQ after rproc_del → vq UAF | core SRCU: remoteproc_virtio.c:99-106 (ffe14d610cb0) | RESOLVED by core; v3 changelog should say so |
| S2-9 | Sashiko v2 rproc | da_to_va falls through after ATT match | sunxi_rproc.c:342 | RESOLVED |
| S2-10 | Sashiko v2 rproc | Boot address write not flushed | sunxi_rproc.c:220 | RESOLVED |
| S2-11 | Sashiko v2 bindings | r_sram required; A527 compatible; interrupt-names | all reg required; single compatible | RESOLVED |
| S2-12 | Sashiko v2 tests | BE mocks, Kconfig deps, false-positive kick test | readl in tests; `depends on` driver; test renamed to null-chan safety | RESOLVED |

New-code check on the fixes: the workqueue removal (45501e5ae8a4) undid the context fix
that the v2-era workqueue provided — see F1.

## Findings

### F1: rpmsg receive path runs in hardirq context under a spinlock
- Severity: Critical
- Confidence: CONFIRMED
- Stage: S2
- Location: drivers/remoteproc/sunxi_rproc.c:57-63, drivers/mailbox/sun55i-msgbox.c:77,113
- Contract/evidence: include/linux/mailbox_client.h:24 ("rx_callback: Atomic callback");
  drivers/mailbox/mailbox.c:149-154 calls it synchronously; drivers/remoteproc/remoteproc_virtio.c:90-108
  → vring_interrupt → drivers/rpmsg/virtio_rpmsg_bus.c:682 `mutex_lock(&vrp->endpoints_lock)` and :694 `mutex_lock(&ept->cb_lock)`.
- Failure scenario: E906 writes a kick → GIC SPI 0 → sun55i_msgbox_irq() (hardirq, IRQs off,
  mbox->lock held) → mbox_chan_received_data() → sunxi_rproc_mb_rx_callback() →
  rproc_vq_interrupt() → rpmsg_recv_single() → mutex_lock() → "BUG: sleeping function called
  from invalid context" on every received message on a !PREEMPT_RT kernel. The board config
  has CONFIG_PREEMPT_RT=y, which force-threads the handler, so silicon testing cannot see it.
  imx_rproc's identical direct call (imx_rproc.c:863) is only safe because imx-mailbox
  delivers from a threaded handler (imx-mailbox.c:658).
- Fix: either (a) restore deferral in the client (dedicated `alloc_workqueue(..., WQ_HIGHPRI)`
  like stm32_rproc.c:255-276), which honours the mailbox_client contract, or (b) make the
  msgbox deliver from the thread half of `request_threaded_irq(..., IRQF_ONESHOT)` with the
  spinlock dropped around mbox_chan_received_data(), and say in the rproc commit message that
  it relies on that (imx precedent). Run once on a non-RT kernel with DEBUG_ATOMIC_SLEEP and
  PROVE_LOCKING before sending.

### F2: Required MMIO regions treated as optional; core can boot from an unprogrammed vector
- Severity: High
- Confidence: CONFIRMED
- Stage: S5
- Location: drivers/remoteproc/sunxi_rproc.c:422-428, 441-448, 451-457, 218-222
- Contract/evidence: rproc.yaml:24-36 and `required: reg, reg-names` make all four regions mandatory.
- Failure scenario: `cfg` mapping fails or the entry is missing → dev_warn only → start()
  skips the STA_ADD write at :218 → reset released at :224 → E906 executes from whatever
  vector was left in the register. Missing `remap` silently leaves SRAM space 1 unshared.
- Fix: `devm_platform_ioremap_resource_byname()` for cfg/remap (and `devm_ioremap_wc` with
  failure → `-ENOMEM` for both SRAMs), fail probe on error, drop the `if (priv->cfg_va)` /
  `if (priv->remap_va)` guards.

### F3: Remap register: inconsistent description and ad-hoc mapping inside PRCM
- Severity: High
- Confidence: CONFIRMED (inconsistency) / PLAUSIBLE (sharing hazard)
- Stage: S5
- Location: drivers/remoteproc/sunxi_rproc.h:39-43, sunxi_rproc.c:139-147, 186-192; sun55i-a523.dtsi:1176
- Contract/evidence: cover letter "Open Architectural Design Question" says bit 0 controls reset
  and boot translation; header comment says the register is in "PRCM_R_CCU / MCU_CCU"; code only
  ever toggles bit 1 (SUNXI_REMAP_SRAMA3_2_BIT), bit 0 is defined and unused. 0x07010364 sits in
  the 0x07010000 PRCM page beside r_ccu (dtsi:932-934, size 0x250) and nmi_intc.
- Failure scenario: reviewers cannot tell what the register does; any future PRCM user that
  touches the same word races the unlocked readl/writel RMW at :140-143 / :187-190.
- Fix: settle from the user manual which bit is needed and fix the cover letter and header to
  match. Ask Chen-Yu before v3 whether he wants it exposed by the r-ccu driver (e.g. as a
  reset/gate) or the sunxi SRAM controller instead of a 4-byte `reg`.

### F4: Commit messages and cover letter state things the code does not do
- Severity: Medium
- Confidence: CONFIRMED
- Stage: S1
- Location: v3-0001 body; v3-0005 body; v3-0000
- Contract/evidence: Part 1 ground rule 1.
- Failure scenario:
  - v3-0001: "sun55i (A523, A527, T527) and sun60i (A733) SoCs" — A733 was dropped.
  - v3-0005: "Workqueue-deferred RPMsg notification for Linux PREEMPT_RT safety" — no workqueue
    (sunxi_rproc.c:57-63). "Ring-buffer trace0 diagnostic endpoint via debugfs" — the core
    creates trace debugfs from an RSC_TRACE entry; the driver adds nothing.
  - v3-0000: "32 standalone unit tests for remoteproc and 22 for mailbox" — 0003/0006 messages
    say 34 and 32. "rproc_shutdown() and recovery paths call rproc_resource_cleanup()" —
    rproc_boot_recovery() only calls rproc_stop()/rproc_start() (remoteproc_core.c:1814,1829).
    "rproc_del() first ... suppress any outgoing kicks/callbacks" overstates it; the real
    protection is the core SRCU. The E907→E906 rename across versions is not explained.
  - Changelog is not mapped to reviewer comments (KK11).
- Fix: rewrite the three texts from the code; add a per-reviewer "Changes in v3" list.

### F5: Krzysztof's v1 binding points only partly applied
- Severity: Medium
- Confidence: CONFIRMED
- Stage: S6
- Location: rproc.yaml:39,46,53,60; v3-0001 and v3-0004 subjects
- Contract/evidence: v1/emails/18 ("Nope, this cannot be flexible"; "drop ... redundant 'binding'");
  DTS always supplies all three clocks/resets (sun55i-a523.dtsi:1178-1185).
- Failure scenario: one compatible, one SoC, yet `sram` clock/reset optional → same objection a
  third time. Subjects ending in "schema" repeat the redundant-word nit.
- Fix: remove `minItems: 2` from clocks/clock-names/resets/reset-names and make the driver use
  the non-optional getters (sunxi_rproc.c:581, 594). Subjects: e.g.
  "dt-bindings: mailbox: Add Allwinner A523 message box" and
  "dt-bindings: remoteproc: Add Allwinner A523 RISC-V remote processor".

### F6: Kick throughput capped by strict last_tx_done + 1 ms polling; bursts overflow the queue
- Severity: Medium
- Confidence: PLAUSIBLE (needs a before/after benchmark)
- Stage: S4
- Location: sun55i-msgbox.c:204-220, 318-320; sunxi_rproc.c:266-278
- Contract/evidence: mailbox core holds the channel busy until a poll tick sees
  last_tx_done() true; queue depth MBOX_TX_QUEUE_LEN = 20 (mailbox_controller.h:115);
  sun6i-msgbox uses the same strict semantics with a 5 ms period (sun6i-msgbox.c:268-269).
- Failure scenario: Linux sends kick A; E906 has not yet drained FIFO; kick B waits for the next
  1 ms poll. Back-to-back kicks are therefore limited to roughly one per poll tick, and more than
  20 queued kicks return -ENOBUFS and log "failed to send mailbox kick". This is the likely cause
  of the throughput drop seen after 77e31da7bb9f.
- Fix: kicks are idempotent doorbells, so coalesce in the client: skip the send when a kick for
  that vring is already pending (e.g. use `tx_done` to clear a per-vring pending bit), or use the
  hardware write-IRQ (SUNXI_MSGBOX_WRITE_IRQ_* are defined but unused) for txdone_irq instead
  of polling. Keep last_tx_done strict.

### F7: memory-region[0] used both as DMA pool and as a static carveout; da_to_va duplicates the core
- Severity: Medium
- Confidence: CONFIRMED
- Stage: S4
- Location: sunxi_rproc.c:482, 485-530, 89-101, 345-382
- Contract/evidence: remoteproc_virtio.c:452 already initialises region 0 as the vdev DMA pool;
  imx_rproc skips the vdev buffer region when registering carveouts; rproc_da_to_va() in the
  core searches rproc->carveouts after ops->da_to_va.
- Failure scenario: no crash today (both mappings are WC), but the whole vram region appears as
  a carveout overlapping every dma_alloc_coherent() vring/buffer, and the second half of
  da_to_va repeats lookups the core does. Reviewers will ask why.
- Fix: do not register region 0 as a carveout; delete sunxi_rproc.c:345-376 and the
  dram_*/trace_* fields; replace the `(u32)rmem->phys` cast with a range check.

### F8: msgbox takes ownership of the other processors' banks and resets
- Severity: Medium
- Confidence: PLAUSIBLE
- Stage: S5
- Location: sun55i-msgbox.c:272-294; sun55i-a523.dtsi:608-629
- Contract/evidence: devm_reset_control_array_get_shared + devm action asserts all four resets
  on unbind, including RST_BUS_R_MSGBOX in the CPUS (R) domain.
- Failure scenario: unbinding or failing to probe the Linux msgbox asserts the CPUS bank reset
  while CPUS firmware (e.g. an SCP) still uses its bank. Also, why this is not an extension of
  sun6i-msgbox.c is not explained.
- Fix: justify the one-node/four-bank model and the sun6i question in the 0002 commit message;
  consider leaving the remote banks' resets deasserted (do not assert on remove).

### F9: Crash interrupt with no DTS user and no evidence of a hardware line
- Severity: Low
- Confidence: PLAUSIBLE
- Stage: S6
- Location: rproc.yaml:93-99; sunxi_rproc.c:65-78, 608-620
- Contract/evidence: no `interrupts` in sun55i-a523.dtsi:1171-1188.
- Failure scenario: dead code that drew three High Sashiko findings in v2.
- Fix: drop from binding and driver unless the SoC manual names the line; add later with a user.

### F10: Undocumented ATT aliases
- Severity: Low
- Confidence: PLAUSIBLE
- Stage: S5
- Location: sunxi_rproc.c:33-45; sunxi_rproc.h:13-17
- Contract/evidence: cover letter documents Space 0 at DA 0x3FFC0000 and Space 1 at 0x40000000
  only; the table also maps 0x3FF80000, 0x00020000 and 0x40040000, plus host-PA identity entries.
- Failure scenario: a firmware segment linked at an alias that is not real hardware is loaded
  without error.
- Fix: keep only aliases the manual documents and cite it in a comment.

### F11: Small cleanups
- Severity: Low
- Confidence: CONFIRMED
- Stage: S1
- Location: sunxi_rproc.c:22 (`of_address.h` unused); sun55i-msgbox.c:10 and sun55i-msgbox.h:5
  (`bitfield.h` unused); sun55i-msgbox.c:71-76 (comment about multiple cores interrupting, only
  one IRQ is requested); sunxi_rproc.c:568-570 (dead `if (!priv->cfg)` fallback);
  drivers/mailbox/Kconfig:338 (`default ARCH_SUNXI` for an optional driver);
  sunxi_rproc.c:548 (generic `riscv-firmware.elf` default name in /lib/firmware root);
  `#mbox-cells` description (msgbox.yaml:69-72) does not say each channel is bidirectional,
  while the DTS spends two channels (8 rx, 9 tx).
- Fix: as listed.

### F12: KUnit patches cost more review than they return
- Severity: Low
- Confidence: CONFIRMED
- Stage: S7
- Location: v3-0003, v3-0006; sun55i-msgbox.h:44-51, sunxi_rproc.h:112-122
- Contract/evidence: no driver-level KUnit suites exist in drivers/remoteproc or drivers/mailbox in
  the base tree; the suites (~1,520 lines) force VISIBLE_IF_KUNIT/EXPORT_SYMBOL_IF_KUNIT across
  both drivers; Sashiko found test bugs in both prior rounds; none of them exercises execution
  context, so F1 passed. Kconfig help (msgbox Kconfig) still describes only routing/offset tests.
- Fix: drop 0003 and 0006 from the upstream series and keep them in the board tree.

## Checked and clean
- Bisectability — bindings (0001, 0004) precede drivers, tests follow drivers, DTS last; Part 6 builds per commit without errors.
- Teardown order — rproc_del() before mailbox free (sunxi_rproc.c:675-677) is correct given core SRCU (remoteproc_virtio.c:99-106) and matches stm32_rproc; the agents rules that demand the opposite (upstream-patch-submissions.md:47-49, linux-kernel-review-prompt.md:98) are wrong.
- msgbox startup/shutdown vs ISR (Gemini F2) — not a race: the ISR only touches a channel whose enable bit is set and holds mbox->lock throughout (77-120); shutdown clears the bit under the lock (188-192) before draining; startup drains before setting it (157-172); status bits are per-channel write-1-to-clear.
- Reserved-memory release on parse failure (Gemini verdict) — has_reserved_mem is set at sunxi_rproc.c:483 before the loop, so probe's err_mem_release (660-662) releases it.
- Carveout lifecycle — added in prepare (159-160) after all fallible steps, freed by err_free_mem (172-174) on failure, rebuilt each boot after rproc_resource_cleanup.
- Crash IRQ arm/disarm — atomic bit, no lock across disable_irq (72-73, 230-232, 249-251).
- Kick payload lifetime — static const per vring (263, 274-275).
- last_tx_done drain semantics — count == 0 (218-219), same policy as sun6i-msgbox.
- da_to_va overflow guard — 315.
- Clock/reset per bank for msgbox — 280-294; DT header macros exist in base (Part 6 dt_binding_check exit 0).
- DTS node order and unit addresses — mailbox@3003000 between 3002000 and 3006000 (dtsi:608); remoteproc@7130000 after npu@7122000 (dtsi:1171).

## NEEDS_CONTEXT
- A523 user manual :: PRCM register 0x0364 bit definitions — to settle F3.
- A523 user manual :: E906 interrupt lines to the GIC — to settle F9.
- A523 user manual :: E906 address map — to settle F10.
- Board benchmark :: ping_rpmsg throughput before/after 77e31da7bb9f and with coalesced kicks — to settle F6.
- scripts/spdxcheck.py :: needs `python3-ply`; Part 6 shows it never ran, so SPDX tags were not machine-checked.

## Machine summary
```json
{"verdict": "NOT READY", "findings": [
 {"id": "F1", "severity": "Critical", "confidence": "CONFIRMED", "stage": "S2", "location": "drivers/remoteproc/sunxi_rproc.c:57", "title": "rpmsg receive path runs in hardirq under spinlock (mutex_lock in rpmsg_recv_single)"},
 {"id": "F2", "severity": "High", "confidence": "CONFIRMED", "stage": "S5", "location": "drivers/remoteproc/sunxi_rproc.c:422", "title": "Required cfg/remap/r_sram1 mappings optional; boot vector may be unprogrammed"},
 {"id": "F3", "severity": "High", "confidence": "CONFIRMED", "stage": "S5", "location": "drivers/remoteproc/sunxi_rproc.h:39", "title": "Remap register bit/ownership inconsistent and mapped ad hoc inside PRCM"},
 {"id": "F4", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S1", "location": "v3-0005", "title": "Commit messages and cover letter contradict the code"},
 {"id": "F5", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S6", "location": "Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml:39", "title": "Krzysztof v1 points partly applied: flexible clocks/resets, 'schema' in subjects"},
 {"id": "F6", "severity": "Medium", "confidence": "PLAUSIBLE", "stage": "S4", "location": "drivers/mailbox/sun55i-msgbox.c:318", "title": "Kick throughput capped by strict last_tx_done + 1 ms poll; queue overflow"},
 {"id": "F7", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S4", "location": "drivers/remoteproc/sunxi_rproc.c:482", "title": "DMA pool region also registered as carveout; da_to_va duplicates core"},
 {"id": "F8", "severity": "Medium", "confidence": "PLAUSIBLE", "stage": "S5", "location": "drivers/mailbox/sun55i-msgbox.c:284", "title": "msgbox asserts other processors' bank resets on unbind"},
 {"id": "F9", "severity": "Low", "confidence": "PLAUSIBLE", "stage": "S6", "location": "drivers/remoteproc/sunxi_rproc.c:608", "title": "Crash IRQ has no DTS user or documented hardware line"},
 {"id": "F10", "severity": "Low", "confidence": "PLAUSIBLE", "stage": "S5", "location": "drivers/remoteproc/sunxi_rproc.c:33", "title": "Undocumented ATT aliases"},
 {"id": "F11", "severity": "Low", "confidence": "CONFIRMED", "stage": "S1", "location": "drivers/remoteproc/sunxi_rproc.c:22", "title": "Unused includes, stale comment, dead fallback, Kconfig/firmware defaults"},
 {"id": "F12", "severity": "Low", "confidence": "CONFIRMED", "stage": "S7", "location": "drivers/remoteproc/sunxi_rproc_test.c:1", "title": "KUnit patches: high review cost, missed F1; drop from upstream series"}
]}
```
