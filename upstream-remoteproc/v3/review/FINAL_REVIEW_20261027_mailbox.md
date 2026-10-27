# sun55i-msgbox: pre-submission review (linux-cubie 91018e3b423e)

Scope: drivers/mailbox/sun55i-msgbox.{c,h}, sun55i_msgbox_test.c, Kconfig/Makefile entries,
allwinner,sun55i-a523-msgbox.yaml, msgbox node in sun55i-a523.dtsi, and the client in
drivers/remoteproc/sunxi_rproc.c. Checked against drivers/mailbox/mailbox.c in the same tree.
checkpatch --strict -f: clean on all three files.

## Bug

### B1. KUnit: generic_handle_irq() called from preemptible task context with IRQs on
sun55i_msgbox_test.c:1126 `generic_handle_irq(rv->irq);` (test_tx_irq_handler_reports_tx_done).
KUnit cases run in a kthread with interrupts enabled. The flow-handler path assumes hardirq context:
- kernel/irq/handle.c:214 `if (WARN_ONCE(!irqs_disabled(), "irq %u handler %pS enabled interrupts\n", ...)) local_irq_disable();`
  fires on every kernel, because IRQs were never disabled. It then returns with IRQs *disabled*
  (local_irq_disable()) into the test kthread.
- handle_simple_irq() (kernel/irq/chip.c:614) takes `desc->lock` with a plain raw_spin_lock; the lock
  class is shared by all irq descs and is used in hardirq context, so PROVE_LOCKING reports
  inconsistent lock state ({IN-HARDIRQ-W} -> {HARDIRQ-ON-W}).
- kstat_incr_irqs_this_cpu() and add_interrupt_randomness() (`this_cpu_ptr`, drivers/char/random.c:1103)
  trigger DEBUG_PREEMPT "using smp_processor_id() in preemptible" reports.
The test still passes (KUnit doesn't fail on WARN), so the run looks green. But the kernel is
tainted and any 0-day or KernelCI lockdep run will report it.
Fix: `generic_handle_irq_safe(rv->irq)` (kernel/irq/irqdesc.c:705, does local_irq_save around it).

### B2. KUnit: dummy IRQ is not requestable on arm32 or powerpc, so 3 cases fail
sun55i_msgbox_test.c:1089-1092 allocate a desc with irq_alloc_desc() + irq_set_chip_and_handler().
New descs get IRQ_DEFAULT_INIT_FLAGS = ARCH_IRQ_INIT_FLAGS (include/linux/irq.h:603, kernel/irq/irqdesc.c:130),
which is `IRQ_NOREQUEST | IRQ_NOPROBE` on arm (arch/arm/include/asm/hw_irq.h:15) and `IRQ_NOREQUEST`
on powerpc. request_irq() then returns -EINVAL (kernel/irq/manage.c:2149), so these cases fail:
test_tx_irq_startup_enables_rx_after_line, test_tx_irq_handler_reports_tx_done and
test_tx_irq_line_shared_by_channels. Kconfig `depends on ARCH_SUNXI || COMPILE_TEST` plus
`default KUNIT_ALL_TESTS` means a 32-bit sunxi build (multi_v7 + KUNIT_ALL_TESTS) or a ppc COMPILE_TEST
KUnit run gets these failures.
Fix: `irq_clear_status_flags(irq, IRQ_NOREQUEST | IRQ_NOPROBE);` after irq_set_chip_and_handler().
Optionally also restrict the driver to `(ARCH_SUNXI && ARM64) || COMPILE_TEST`, because the A523 is arm64-only.

## Should-fix

### S1. Poll mode: held-message write is not serialised against send_data(), so a full FIFO can be written
sun55i-msgbox.c:309-326 (send_data, timer branch) and :436-444 (last_tx_done) check the count and write
the FIFO with no common lock. send_data() runs under chan->lock (mailbox.c msg_submit). last_tx_done()
runs from txdone_hrtimer() with no lock (mailbox.c:113-116), selected by a lockless `active_req` check.
They overlap on the path the comment at :304-308 says it handles. A tx_block client times out, and
mbox_send_message() calls tx_tick(chan, -ETIME) in process context (mailbox.c:290-292). That clears
active_req and calls msg_submit() -> send_data(new). Meanwhile the hrtimer on another CPU is already
inside last_tx_done() for the old request.
Interleaving with FIFO count = 7:
CPU B (hrtimer): :436 count 7 < 8 -> :440 test_and_clear_bit(tx_held) true;
CPU A (send_data): :309 clear_bit (no-op) -> :317 count 7 < 8 -> :329 writel(new);
CPU B: :441 writel(held) -> 9th write into an 8-deep FIFO. The driver's own rule ("Writing a full FIFO
corrupts its count, so never do it", :312) is broken, and the count is corrupted (reference §6a).
There is also a plain data race on `tx_held_msg[n]` (:322 store vs :441 load) that KCSAN will report.
It is latent today, because sunxi_rproc uses tx_block = false (sunxi_rproc.c:721). But the driver
explicitly claims to handle the timed-out case.
Fix: in timer mode, take mbox->lock around "check count, write FIFO, update held state" in both
send_data() and last_tx_done(). Then tx_held can be a plain bitmap under the lock and the
smp_mb__before_atomic() at :324 goes away.

### S2. Probe does not enforce the four bank clocks and resets the hardware needs
sun55i-msgbox.c:516 `devm_clk_bulk_get_all_enabled(dev, &clks)` and :520
`devm_reset_control_array_get_shared(dev)` accept any number of clocks and resets, including one.
A DT with only the host bank clock and reset (exactly the old D12 mistake) still probes. The RV bank then
stays gated, and the driver's own comment (:511-515) says an unclocked bank "drops writes and reads back
as zero". So every kick is lost and last_tx_done() (:436) always reads count 0 and reports success:
silent failure, no error. dtbs_check catches it only if someone runs it on that DT.
Fix: check `ret != SUN55I_MAX_PROCESSORS` and fail with dev_err_probe(-EINVAL). Or use named
devm_clk_bulk_get_enabled() with `arm/dsp/cpus/rv`, which the binding already requires
(clock-names/reset-names).

### S3. KUnit cannot catch regressions in the TX-done teardown and retry paths
- The second disarm in shutdown (sun55i-msgbox.c:411-417) exists for a TX handler that re-arms through
  tx_tick() -> msg_submit() -> send_data() while shutdown waits in free_irq()/synchronize_irq().
  No test exercises it: test_tx_irq_handler_reports_tx_done has no queued message (msg_count 0), so
  msg_submit() never calls send_data(). Deleting line 417 leaves every test green.
- The -EBUSY retry (:318-320) is checked only at the register level
  (test_tx_irq_armed_after_every_send). No test drives the core: mbox_send_message() on a full FIFO,
  then the TX IRQ, then tx_tick() with active_req == MBOX_NO_MSG, then a resubmit.
- test_tx_irq_startup_failure_leaves_rx_off (:1192) does not model what the core does next
  (mbox_free_channel() -> shutdown(), mailbox.c:331-336), which is the reason tx_irq_held exists. Call
  shutdown() after the failed startup and assert `users == 0` and that free_irq was not called.
The fixture already wires chans[] to the core (create_tx_irq_fixture), so all three are a few lines.

## Nit

N1. sun55i-msgbox.c:61-75. The out-of-range branch of sun55i_chan_to_route() cannot be reached
(`n = chan - controller.chans`, and the index comes from fw_mbox_index_xlate(), which bounds-checks,
mailbox.c:512). If it were reached, it would route to regs[0]/port 0, so send_data() would push into
the host's *own* receive FIFO. Silently doing the wrong thing is worse than crashing. Drop the check
(and test_chan_to_route_invalid_channels, which pins this behaviour as "safe"), or make it
`if (WARN_ON_ONCE(...))` with an error return. Better still, compute the route once and store it per
channel (con_priv -> per-channel struct, as other drivers do) instead of 4 out-params in every op.

N2. sun55i-msgbox.c:45/53. With CONFIG_KUNIT, `sun55i_readl` / `sun55i_writel` become global
exported symbols with SoC-generic names. Rename them to `sun55i_msgbox_readl/writel`.

N3. sun55i-msgbox.h:22-37. The header mixes prefixes: `SUNXI_MSGBOX_*`, `SUN55I_*`, and unprefixed
`RD_IRQ_*`, `WR_IRQ_*`, `MSG_NUM_MASK`. Use one prefix (`SUN55I_MSGBOX_`).

N4. `SUN55I_PROC_ARM` is defined (.h:11) but the code uses the literal `mbox->regs[0]` 9 times
(.c:87, 281, 376, 378, 396, 398, 458, 545, 546).

N5. sun55i-msgbox.c:127-147 / 92-95. The RX handler takes the lock and reads READ_IRQ_ENABLE and
READ_IRQ_STATUS once per channel: 12 lock round trips and 24 MMIO reads per interrupt, even when one
channel is pending. Read enable & status once per port, then drain only the set bits.

N6. sun55i-msgbox.c:196, 202-208. `done[SUN55I_NUM_CHANS]` holds at most SUN55I_CHANS_PER_PROC
entries. The route loop scans all three routes to find the one for `tx->remote_id`. Store local_n and
remote_n in struct sun55i_tx_irq at probe instead.

N7. sun55i-msgbox.c:360-363. The WRITE_IRQ_STATUS clear in startup() is redundant, because
send_data() -> tx_irq_enable(true) clears it right before every arm (:182).

N8. sun55i-msgbox.c:6. "Based on vendor sunxi-msgbox driver by Allwinner Technology Co., Ltd." but no
Allwinner copyright line. If code was derived from it, keep the original copyright. If only the register
layout was taken, say "register layout from" instead.

N9. sun55i-msgbox.h:59. "Protects concurrent MMIO register access" is vague. Say what the lock protects:
RMW of READ/WRITE_IRQ_ENABLE, and the RX drain plus pending clear (and the held-message state if S1 is
taken).

N10. sun55i-msgbox.c:579. TX line names `<dev>-tx1/2/3` use bank indices. `-dsp/-cpus/-rv` (the
binding's interrupt-names) read better in /proc/interrupts.

N11. Header comment (:3) and MODULE_DESCRIPTION (:615) say "4-Port", but in the code "port" means the
per-bank link index (local_n/remote_n), and the hardware has 4 banks with 3 links each. Use "Message Box".

N12. sun55i-msgbox.c:16. Drop `<linux/kernel.h>`. Include what is used directly: `<linux/bitops.h>`
(set_bit etc.), `<linux/string.h>` (memcpy), `<linux/mutex.h>`.

N13. KUnit: the fixture leaves `mbox.irq = 0`, so every shutdown() runs `synchronize_irq(0)` on a real
descriptor where one exists (x86: the timer). Point it at the dummy desc from mock_alloc_irq().

N14. KUnit: in the tx-irq fixture `controller.dev` is NULL, so the failure path at :251 prints through
`dev_err(NULL, ...)`. Set a dummy device (kunit_device_register()).

N15. KUnit: test_tx_irq_line_shared_by_channels and test_tx_irq_handler_reports_tx_done request the
line through startup() and release it only at the end. If a KUNIT_ASSERT aborts in between,
mock_free_irq_desc() frees a desc that still has an action attached. Register the shutdown as a
kunit action, or use EXPECT after the startup.

N16. KUnit file name: Documentation/dev-tools/kunit/style.rst:205-213 recommends `<suite>_kunit.c`
(e.g. `sun55i-msgbox_kunit.c`). `_test.c` has precedent, so this is only an optional cleanup.

## Checked and found correct (no finding)
- Startup failure: the core calls shutdown() through mbox_free_channel() (mailbox.c:331-336). tx_irq_held
  prevents an unbalanced tx_irq_put(), and the RX enable is never set before the line is held.
- Lock order is always chan->lock -> mbox->lock (msg_submit -> send_data -> tx_irq_enable). Both handlers
  drop mbox->lock before calling into the core, so there is no inversion with rx callbacks that send.
- Shutdown: RX disable under the lock, then synchronize_irq() before the core clears chan->cl. TX: disarm,
  free_irq()/synchronize_irq(), disarm again. tx_irq_lock serialises request/free against concurrent
  startup() (under con_mutex) and mbox_free_channel() (not under con_mutex).
- devm teardown: controller unregister (shuts down every channel, frees TX lines, cancels hrtimer)
  -> free RX irq -> clocks off -> iounmap. No IRQ can run after anything it uses is gone. All read and
  write enables are masked before devm_request_irq().
- Unbind while the rproc holds the channel: fw_devlink creates a managed link from `mboxes`
  (drivers/of/property.c:1374), so the consumer is unbound first.
- Channel index from DT: bounded by the core's default fw_mbox_index_xlate().
- Barriers: writel() orders the vring writes before the doorbell, and readl() orders the RX read before
  the vring reads. Relaxed accessors would be wrong here. smp_mb__before_atomic()/test_and_clear_bit()
  pairing is correct (S1 aside).
- RX: drain bounded at 8, pending cleared after the drain. A late arrival re-raises the level IRQ.
- Binding/DT: header constants exist, node is sorted by address, example matches the dtsi, and
  the owner decisions (four required interrupts, tx-done-irq property, reset never asserted) were not
  re-raised.
