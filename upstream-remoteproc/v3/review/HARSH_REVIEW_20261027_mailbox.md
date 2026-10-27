# sun55i msgbox pre-submission review (v3-next-20261026 @ 3a05cbdb8156)

Slice: 5d1e4367b33c (binding), 1fc549446580 (driver), 91500316b05e (KUnit).
Framework read at the same ref: drivers/mailbox/mailbox.c, include/linux/mailbox_controller.h,
include/kunit/static_stub.h, include/kunit/test-bug.h. Peers: sun6i-msgbox.c, omap-mailbox.c.
Line numbers are `git show v3-next-20261026:<path>`.

Checked and found no defect: lock order (core `chan->lock` -> `mbox->lock` in send_data; the RX and
TX ISRs drop `mbox->lock` before calling into the core, so there is no ABBA), the RX drain-then-W1C
order, the held-message barrier (`smp_mb__before_atomic`+`set_bit` / fully ordered
`test_and_clear_bit`), the TX ISR's mask-then-ack order, the core's default xlate bounds check
(`fw_mbox_index_xlate`), probe devm unwind order (controller unregistered before the IRQ is freed
and before reset/clock), the bit and offset definitions against the reference §3, the dt-bindings
header macros used by the example, and the "26 tests" count.

---

## Driver: drivers/mailbox/sun55i-msgbox.c

### D1. Medium: when startup() fails, a running RX ISR can call the client after the core has set `chan->cl = NULL`
Lines 348-372:
```c
	/* Enable receive IRQ */
	spin_lock_irqsave(&mbox->lock, flags);
	val |= RD_IRQ_EN_BIT(p);
	...
	if (mbox->tx_done_irq) {
		...
		ret = sun55i_msgbox_tx_irq_get(mbox, remote_id);
		if (ret) {
			spin_lock_irqsave(&mbox->lock, flags);
			val &= ~RD_IRQ_EN_BIT(p);
			...
			return ret;
		}
```
The RX interrupt is enabled before the only step that can fail. The error path disables it but
does not call `synchronize_irq(mbox->irq)`. The core then runs `mbox_clean_and_put_channel()`
(mailbox.c:343-345), which sets `chan->cl = NULL`.

Failure: in `allwinner,tx-done-irq` mode the remote has a message queued. On CPU1 the SPI 0 handler
drains it in `rx_drain()`, drops the lock, and reaches `mbox_chan_received_data()` (driver :143).
Meanwhile CPU0's `request_irq()` fails (for example, the line is already claimed), the error path
clears the enable bit, the core sets `cl = NULL`, and CPU1 dereferences `chan->cl->rx_callback`
(mailbox.c:152). That is a NULL dereference, or at best a message delivered to a client whose bind
failed. The error path also leaves the TX IRQ armed in the remote bank if the client already
replied from its rx callback.

Fix: get the TX line before enabling the RX IRQ (nothing then needs unwinding). Otherwise, call
`synchronize_irq(mbox->irq)` and disarm the write IRQ on the error path.

### D2. Medium: unbind and probe-deferral assert the resets and gate the clocks of banks Linux does not own
Lines 508-522:
```c
	ret = devm_clk_bulk_get_all_enabled(dev, &clks);
	rst = devm_reset_control_array_get_shared(dev);
	ret = reset_control_deassert(rst);
	ret = devm_add_action_or_reset(dev, sun55i_msgbox_assert_reset, rst);
```
On unbind or probe failure, devm asserts all four bank resets and gates all four bus clocks. No other
Linux consumer holds the shared resets, so the deassert count drops to 0 and the reset takes effect.
Reference §6 board check confirms it: "After unbinding … all four read 0x00000000 (gate off, reset
asserted)".

The DSP and CPUS banks are the receive banks of the DSP and CPUS. Traffic between DSP, CPUS and RV
(and CPUS firmware that talks to the DSP) goes through them without involving Linux.
`unbind`/`modprobe -r`, or an `-EPROBE_DEFER` from `platform_get_irq()` (:544, :565) after the
deassert, wipes those FIFOs and blocks that traffic until Linux probes again.

sun6i-msgbox avoids exactly this: "we share this hardware block with other firmware … Therefore, we
do *not* assert the reset line if probing fails or when removing the device".

Fix: deassert without registering the assert action, at least for the remote banks. Also consider
`CLK_IS_CRITICAL`-like handling, or leaving the clocks on at remove. Related to D12 (see the
conflicts section).

### D3. Medium (maintainer pushback): timer mode breaks the documented `send_data`/`last_tx_done` contract
Lines 310-319 and 434-439:
```c
	if (sun55i_msgbox_tx_count(...) >= SUN55I_FIFO_MAX) {
		...
		mbox->tx_held_msg[n] = msg;
		smp_mb__before_atomic();
		set_bit(n, &mbox->tx_held);
		return 0;
	}
...
	if (test_and_clear_bit(n, &mbox->tx_held)) {
		sun55i_writel(mbox->tx_held_msg[n], ...MSG_FIFO...);
```
mailbox_controller.h:23-26 says `send_data` "Returns 0 if data is accepted for transmission,
-EBUSY while rejecting", and :44-49 says that `last_tx_done` polls status and that "Only in polling
mode 'send_data' is expected to return -EBUSY". The driver does the opposite in both modes:
- In poll mode it returns 0 for a message it did not send, and `last_tx_done()` has a side effect
  (it does the actual transmission from hrtimer context).
- In IRQ mode it returns -EBUSY.

The driver ends up with a private one-deep TX queue that duplicates the core's. It exists only
because `msg_submit()` starts the poll timer only when `!err` (mailbox.c:77). Jassi will ask for that
core fix: start the poll timer on -EBUSY in `MBOX_TXDONE_BY_POLL`. The core fix would delete
`tx_held`, `tx_held_msg[]` and D4/D5 with it. At minimum, the commit message must justify the
deviation.

### D4. Low: a held message is written after a newer one (stale and out of order) after a tx_block timeout
Lines 315-318 and 434-439. Scenario: a timer-mode client with `tx_block = true, tx_tout = T`.
1. The FIFO is full, so `send_data(A)` holds A and returns 0.
2. The receiver stalls for longer than T, so `mbox_send_message()` calls `tx_tick(chan, -ETIME)`
   (mailbox.c:299-301). That sets `active_req = MBOX_NO_MSG`, but `tx_held` bit n is still set.
3. The receiver drains. The client sends B; `send_data(B)` sees room and writes B directly.
4. On the next poll, `last_tx_done()` takes the `test_and_clear_bit` branch and writes A after B.

The remote gets a message the client was told had failed, after its successor. Also, if the FIFO is
still full at step 3, `tx_held_msg[n]` is overwritten while the hrtimer may be reading it.

Fix: in `send_data`, if `tx_held` is set, write or drop the held message first, or refuse. Better,
remove the mechanism (D3).

### D5. Low: shutdown() does not synchronise with the poll hrtimer; a held message can reach the remote after shutdown
Line 400: `clear_bit(n, &mbox->tx_held);`. `txdone_hrtimer()` (mailbox.c:116-122) runs
`last_tx_done()` locklessly. If it already won `test_and_clear_bit` (:435), it writes the FIFO
(:436) after `shutdown()` has returned and the channel has been released, possibly after a new
client's `startup()`.

Fix: serialise the held-message write and `shutdown()` with `mbox->lock`, and re-check a
"started" flag under it.

### D6. Low: the write-IRQ threshold write in startup is a no-op on this silicon
Lines 358-360:
```c
		/* TX done: the write IRQ, raised while one slot is free */
		sun55i_writel(WR_IRQ_THR_ONE_FREE, mbox->regs[remote_id] +
			      SUNXI_MSGBOX_WRITE_IRQ_THRESHOLD(remote_n, p));
```
Reference §6 (board check): "The write-IRQ threshold registers cannot be written from the A55, not
even in the ARM ports of the remote banks." The driver therefore depends on the reset default, or
on whatever the remote firmware programmed. If the E906 firmware sets threshold 3 (8 free),
TX-done arrives only when the FIFO is empty, and the comments at :161-163 and :358 are wrong.

Fix: drop the write, or keep it with a comment saying it is ineffective and that the remote owns
the threshold. Document the dependency.

### D7. Low: the RX ISR does 12 lock round-trips and 24 MMIO reads per interrupt, plus 2 reads per message
Lines 127-132 and 92-95, 102-104. `rx_drain()` takes `mbox->lock` and reads ENABLE and STATUS for
each of the 12 channels, even when one channel is pending. The drain reads `MSG_STATUS` before
every FIFO pop. sun6i reads EN&STAT once. With the 1-IRQ-per-kick rpmsg traffic in §6b, that is
about 26 APB reads per doorbell in hardirq context.

Fix: read EN&STATUS once per port (3×2 reads) and skip ports with no pending bits. Read the count
once and pop that many, still capped at FIFO_MAX.

### D8. Low: KUnit plumbing in the production MMIO path; the test patch cannot be dropped cleanly
Lines 9-10, 43-58, 60-76, 119, 151, 191, 236, 457-465; header :75-84. Every register access goes
through an out-of-line `sun55i_readl()`/`sun55i_writel()` with `KUNIT_STATIC_STUB_REDIRECT`. When
`CONFIG_KUNIT=y`, the ISR, chan_ops and routing helper become global symbols that are exported in
the KUnit namespace.

All of this is in the driver patch (1fc549446580), not in the test patch. The planned cover-letter
line that the KUnit patches "can be dropped … without affecting the drivers" (D15) is therefore
inaccurate: dropping them leaves the hooks behind. Expect Jassi to object to wrapping every MMIO
access for tests (see also T1).

### D9. Nit: `sun55i_chan_to_route()` maps a bad index to the host's own bank
Lines 64-70: for an out-of-range index, `remote_id = 0` and `remote_n = 0`, which is `regs[0]`, the
ARM bank. A caller bug would write a "send" into Linux's own CPUS receive FIFO instead of failing.
The test (test file :53-77) locks in this "safe clamp".

Fix: `WARN_ON_ONCE` and return an error, or drop the check: `n` always comes from
`chan - chans` within `num_chans`.

### D10. Nit: the IRQ is requested before `controller.chans` is set
Lines 548 vs 576. If the handler ran early, it would compute `&NULL[...]` (:129-130) before
`rx_drain` returns -ENODATA. This is harmless today only because the enables were cleared at
:535. Set up the controller fields before `devm_request_irq`.

### D11. Nits
- `tx_irqs[0]` is never used (:560 starts at 1). Size the array as NUM_ROUTES, or comment why.
- `txpoll_period = 1` is set in IRQ mode too (:581).
- `dev_err` in `tx_irq_get` (:251) duplicates the core's "Unable to startup the chan".
- No `mutex_destroy()`.
- Kconfig has no `default ARCH_SUNXI` (sun6i has one).
- `rx_drain()` uses -ENODATA as an internal "nothing pending" code. A bool or a count of 0 would be
  clearer, since a 0 count is already a distinct case.

---

## KUnit: drivers/mailbox/sun55i_msgbox_test.c (+ driver stubs)

### T1. Medium: the static stubs also redirect the real driver's ISR and hrtimer when tests run on the board
Driver :44-58 and test :110-127, :135-141:
```c
	KUNIT_FAIL(kunit_get_current_test(), "register access outside the mock banks");
	return NULL;   /* -> mock_readl() returns 0, mock_writel() drops the write */
```
`KUNIT_STATIC_STUB_REDIRECT` decides from `current->kunit_test` (test-bug.h:44-49), and in hardirq
or softirq context `current` is the interrupted task. Built-in `KUNIT_ALL_TESTS` runs after
initcalls, by which time the real msgbox is bound and remoteproc may be running.

Failure: an SPI 0 interrupt arrives while a sun55i test kthread is on the CPU.
1. The real `sun55i_msgbox_irq()` reads ENABLE and STATUS through `mock_readl()`, which fails the
   test and returns 0.
2. The handler returns IRQ_NONE without draining, and the level line stays asserted.
3. The interrupt re-enters until `note_interrupt()` logs "nobody cared" and disables SPI 0. RX is
   then dead until reboot.

The poll hrtimer's `last_tx_done()` gets 0 counts the same way, and its held FIFO write is dropped.

Fix: in the mock, forward addresses outside the mock banks to `readl()`/`writel()` instead of
failing. Or make the redirect conditional on `in_task()`. Or both.

### T2. Medium: the TX-done handler tests never check that the core was ticked
Test :389-402 (`test_tx_irq_handler_disarms_and_acks`) and :404-426. They check IRQ_HANDLED, the
enable and the status, but nothing records `mbox_chan_txdone()`: `active_req` is MBOX_NO_MSG and
the client has no `tx_done`. Deleting the loop at driver :231-232 (the whole point of
`allwinner,tx-done-irq`) still passes every test.

Fix: set `chans[9].active_req` to a token, give the client a `tx_done` callback that records it,
and assert one call with r == 0.

### T3. Low: the mock FIFO is one register, so ordering and duplication bugs pass
Test :135-153, :155-188. A FIFO read returns the last value stored in the single data word; only
the count is a queue. Every multi-message test sees the same value N times and checks only
`last_msg` or `count`. A driver that delivered `&msgs[0]` on every iteration (driver :143), or in
reverse order, passes `test_irq_clears_pending_after_drain`,
`test_irq_multi_port_burst_interleaved` and `test_irq_fifo_drain_capped_at_max`.

Fix: model each FIFO as a ring of 8, and assert the sequence in `sink->msgs[]`.

### T4. Low: the write-status mock contradicts the measured hardware
Test :174-176: `*r &= ~val;`, a plain W1C. Reference §6a/§4: the write status is level-latched, and
"writing 1 to clear has no effect while the condition holds". `test_tx_irq_armed_after_every_send`
(:373-378) expects the bit for p=1 cleared with count = 7. Real hardware would keep it set and fire
at once, as the driver comment at :168-169 says.

The test therefore asserts a state silicon cannot reach. It also cannot tell "clear then enable"
from "enable then clear", which is the ordering the comment at driver :166-169 is about.

Fix: model the condition (free ≥ threshold ⇒ the status re-sets on clear and on FIFO change).

### T5. Low: `test_irq_delivers_outside_lock` cannot fail as claimed
Test :620-627 and :650-653.
- On UP (`spin_trylock` always succeeds without DEBUG_SPINLOCK) the lock check is a no-op, as the
  comment admits.
- On SMP, a driver that delivered under the lock would self-deadlock in the nested
  `send_data()`→`tx_irq_enable()` (tx fixture) before any EXPECT ran. The result would be a hang or
  a spinlock-recursion BUG, not a failure.
- The comment "The drain and the pending clear finished before the first callback" is checked only
  for the drain. The STATUS register is read after the ISR returns, not inside the callback.

Fix: record READ_IRQ_STATUS inside the callback, and use the timer fixture for the lock probe so
the nested call does not deadlock. Otherwise, state the limitation.

### T6. Low: the IRQ-mode lifecycle is untested, and the fixtures pass IRQ 0 to real IRQ APIs
- `create_mock_fixture()` leaves `mbox.irq = 0`. Every `shutdown()` in the timer tests calls
  `synchronize_irq(0)` on the running kernel's IRQ 0. That works only because IRQ 0 has no
  descriptor on arm64.
- The tx fixture leaves `tx_irqs[].irq = 0`, so `startup()`/`shutdown()` in IRQ mode are never run.
  Nothing covers `tx_irq_get`/`put` refcounting, the startup error unwind (D1), the double disarm in
  shutdown, or the probe-time disarm.
- The commit message lists "TX done by the write interrupt" among the coverage, but only `send_data`
  arming and the handler's register effects are tested.

Fix: stub `request_irq`/`free_irq`/`synchronize_irq` behind wrappers, or factor the refcount so it
can be tested.

### T7. Low: weak or over-named assertions
- `test_tx_irq_handler_ignores_unarmed` (:416-419) does not check that the pending bit for p = 1 was
  left alone. It should expect `wr_sta == 0xA8`.
- `test_functional_shutdown_flushes_and_bounds` (:517-539) does not test the bound: there is no
  `fifo_stuck`, and the count is 3. The commit claims "startup and shutdown dropping stale messages
  (bounded by the FIFO depth)", but only startup's bound is tested (:499-515).
- `test_functional_send_data_null` (:248-258) is fine: NULL is a legal message because MBOX_NO_MSG
  is -1. It needs a comment, since reviewers will assume it is unreachable.
- `#include <linux/io.h>` (:25) is unused.

### T8. Nit: the commit claims "no architecture restriction", but the Kconfig imposes one
Commit message 91500316b05e: "No readl() on plain memory, so no architecture restriction". The
test depends on `SUN55I_MSGBOX`, which depends on `ARCH_SUNXI || COMPILE_TEST`, and `COMPILE_TEST`
depends on `HAS_IOMEM` (init/Kconfig:46-48). It therefore cannot run under kunit.py's default UML
and needs `--arch=arm64` (or x86_64) with COMPILE_TEST. Say so, or add a `.kunitconfig`.

---

## Binding: allwinner,sun55i-a523-msgbox.yaml

No schema-rule defects found: positional `reg`/`clocks`/`resets`/`interrupts` with matching
`*-names`, `additionalProperties: false`, the conditional `minItems: 4` for the remote lines, and
the example headers and macros all exist at the ref.

### B1. Nit: the hardware description doubles as an OS behaviour description
Lines 88-93: "Signal TX done … instead of polling. … Without this property a full FIFO is polled."
This describes what Linux does, not the hardware. See also the conflicts section.

### B2. Nit: `interrupt-names` is not in `required` while the driver depends on fixed positions
The driver uses `platform_get_irq(pdev, i)` by index (driver :544, :565). That is acceptable for a
fixed positional list, but with `minItems: 1` a DT with names in a different order would validate
only if the names matched, and the names are optional. Either require `interrupt-names`, or drop it
(and `reg-names`, which the driver also never reads) to avoid a "names unused" comment.

---

## Commit-message claims vs code
- 1fc549446580: "TX completion is polled (last_tx_done() checks FIFO count < 8)". True, but
  `last_tx_done()` also transmits (D3). The message should say so; it is the most surprising part.
- 1fc549446580 changelog: "Optional TX done by the receiver bank's write interrupt" with threshold
  "one free slot". The threshold is not writable from the A55 (D6).
- 91500316b05e: "TX done by the write interrupt" coverage (T2, T6); "no architecture restriction"
  (T8); "shutdown dropping stale messages (bounded by the FIFO depth)" (T7).
- 5d1e4367b33c: consistent with the schema.

---

## Conflicts with owner decisions
- **B1 / `allwinner,tx-done-irq`.** The property selects driver policy (IRQ vs poll), not hardware.
  Krzysztof is likely to reject it under "DT describes hardware". Owner decision
  "Message box TX completion: the device tree selects timer or ISR" (2026-10-16), with precedent
  `ti,mbox-send-noirq`. Recorded, not argued.
- **D2.** D12 (reopened 2026-10-16) decides that Linux enables all four banks. It does not decide
  what happens on unbind, and the original D12 text raised exactly this hazard ("asserting their
  resets on unbind would break them"), which reference §6 confirms on silicon. Enabling all four
  stays as decided; only the unbind and probe-failure assert/gate is flagged.
- **D3.** The owner decision fixes timer-mode semantics (`count < 8`, 1 ms, `send_data` never writes
  a full FIFO). It does not prescribe the held-message mechanism. A core fix (poll on -EBUSY) would
  satisfy the decision. Flagged because a mailbox maintainer will object to the op-contract
  violation.
- **D8 / T-series.** D15 keeps KUnit in the series and says it can be dropped separately. D8 shows
  the hooks live in the driver patch, so that statement is not quite true as structured.
