# Independent review: v3 series on `v3-next-20261026`

Date: 2026-10-26. Base: `next-20261026` (`f1ac44c32cba`). Series: `1b05531b132e..8fef71f481ad` (7 commits).
Reviewed as mailbox, remoteproc, DT and sunxi maintainers and as a Sashiko-style bot. Framework
code was read from the same ref (`drivers/mailbox/mailbox.c`, `remoteproc_core.c`,
`remoteproc_virtio.c`, `drivers/base/power/common.c`, `runtime.c`). Line numbers refer to files
at `v3-next-20261026`.

## Verdict

**Ready with small fixes.** I found no Critical or High defect. Both drivers follow the framework
contracts on their normal paths. The lock order is consistent (`chan->lock` -> `mbox->lock`, and
`mbox->lock` is never held across a core callback). RX delivery is synchronised with
`shutdown()`. Teardown order is right in both drivers, and `rproc_vq_interrupt()` is already
SRCU-protected in this tree. The bindings, driver and DTS agree, and every header symbol the DTS
uses exists in the sunxi-ng CCU drivers and the PPU header. What remains:
- one plausible race in the optional `allwinner,tx-done-irq` path (finding 1, not used by any
  in-tree DTS);
- a memory-ordering gap on the timer-mode held message;
- several Low robustness gaps;
- a handful of stale comments that a reviewer will notice;
- the exported patches and cover letter still name next-20261020 (TODO already plans the
  re-export).

Findings 1 to 6 are small code changes. Fix 1, 2 and 6 before sending. The rest can go in or wait
for v4.

---

## Findings

### 1. Medium, PLAUSIBLE: tx-done-irq: an in-flight TX handler can re-arm a channel after `shutdown()` disabled it; stale write-IRQ enables are never cleared
`drivers/mailbox/sun55i-msgbox.c:191-234` (handler), `:326-327` (arm after send), `:403-406`
(shutdown), `:260-272` (put), `:517-521` (probe clears only the ARM bank's read enables)

`sun55i_msgbox_tx_irq()` collects channels under `mbox->lock` and calls `mbox_chan_txdone()` after
dropping the lock. Scenario (IRQ mode, two started channels on the same remote bank, so
`users > 1`):
1. CPU B: the TX handler collects chan 9 and drops the lock.
2. CPU A: `mbox_free_channel(chan9)` -> `shutdown()` -> `tx_irq_enable(false)` ->
   `tx_irq_put()` -> `synchronize_irq()` waits for B.
3. CPU B: `mbox_chan_txdone(chan9)` -> `tx_tick()` -> `msg_submit()`. If the client still had
   queued kicks (`msg_count > 0`), this calls `send_data()`, which writes the FIFO and **sets
   `WR_IRQ_EN_BIT(p)` again** (`:326-327`).
4. B returns, A finishes `shutdown()`, and `mbox_clean_and_put_channel()` sets `chan->cl = NULL`
   but does not reset `msg_count` (`mailbox.c:mbox_clean_and_put_channel`).
5. The level write IRQ fires again (the line is still requested for the other channel). The
   handler calls `mbox_chan_txdone(chan9)` -> `msg_submit()` -> `chan->cl->tx_prepare` with
   `cl == NULL`, a NULL dereference.

With `users == 1` (today's single rproc channel) the line is freed, but the enable bit stays set in
the RV bank. The next `startup()` writes the threshold and pending bits but never clears the
enable (`:356-359`), so the line fires as soon as `request_irq()` returns. It is harmless only
because `__mbox_bind_client()` has just zeroed `msg_count`. The same applies to stale enables left
by a previous kernel (kexec) or a previous bind: probe does not clear the remote banks'
`WRITE_IRQ_ENABLE` for the ARM ports.

**Fix:**
- In `shutdown()`, disable the write IRQ again after `tx_irq_put()` returns. No handler can
  re-arm it once the put has synchronised: `tx_irq_enable(false)`, then put/sync, then
  `tx_irq_enable(false)`.
- Alternatively, keep a per-channel "started" bitmap that `shutdown()` clears under `mbox->lock`
  before the sync, and that the handler and `send_data()` check.
- In IRQ mode, have probe (or `startup()`) clear `WR_IRQ_EN_BIT(p)` for the ARM-link ports in
  each remote bank.

### 2. Low, PLAUSIBLE: timer mode: the held message is published without ordering
`sun55i-msgbox.c:314-315` (store `tx_held_msg[n]`, then `set_bit()`), `:428-430` (consumer)

`set_bit()` is an unordered RMW. The consumer runs `last_tx_done()` from the poll hrtimer, which
can already be running on another CPU and reads `chan->active_req` locklessly. It can see the bit
(its `test_and_clear_bit()` is fully ordered) yet load the old `tx_held_msg[n]`: on arm64, a store
followed by a relaxed RMW to a different address may be observed out of order. The result is a
stale doorbell value, the other vring's index. The path is rare: it needs a full FIFO at
`send_data()` time. But Sashiko-type review flags exactly this pattern.

**Fix:** `smp_mb__before_atomic()` between the store and `set_bit()`. Or do the store and the bit
under `mbox->lock` and take it in `last_tx_done()`.

### 3. Low, PLAUSIBLE: a core left running is not stopped before `prepare()` rewrites its SRAM
`drivers/remoteproc/sunxi_rproc.c:91-172` (prepare), `:216-243` (start)

`prepare()` deasserts cfg/sram/debug and clears both SRAMs, but never asserts `rst_core`.
Something may have left the E906 running: a bootloader, a kexec from a kernel that did not stop
it, or a crashed core after `rproc_shutdown()` refused a non-RUNNING state. In that case:
- `memset_io()` and the ELF load overwrite memory under a running core;
- `start()`'s `reset_control_deassert(rst_core)` is a no-op on an already-deasserted reset;
- the new boot address is never fetched.

i.MX/stm32 detect a running core, and many drivers put the core in reset in probe.

**Fix:** `reset_control_assert(priv->rst_core)` at the start of `prepare()` (or once in probe),
before any SRAM access.

### 4. Low, CONFIRMED: vring notifications hard-code notify IDs 0 and 1
`sunxi_rproc.c:54-60` (`rproc_vq_interrupt(rproc, 0/1)`), `:269-281` (`vqid & 1`)

Notify IDs come from `idr_alloc(&rproc->notifyids, ...)` in `rproc_alloc_vring()`. A resource
table with a second vdev, or with more than two vrings, gets IDs 2 and up. Those vrings are never
serviced, and their kicks alias to payload 0 or 1. Mathieu has asked for the generic form before.

**Fix:** walk `rproc->notifyids` with `idr_for_each()`, as `imx_rproc.c:872` and
`xlnx_r5_remoteproc.c:259` do. Either reject `vqid > 1` in `kick()` with a warning, or size the
static payload table to the vring count.

### 5. Low, CONFIRMED: crash IRQ lookup swallows errors
`sunxi_rproc.c:566-578`

`platform_get_irq_byname_optional()` returns `-ENXIO` when the IRQ is absent, but also
`-EPROBE_DEFER` or other errors when it is present and not ready. Every negative value is treated
as "no crash IRQ". A failed `devm_request_threaded_irq()` only warns. Both make the probe succeed
silently without a configured interrupt.

**Fix:** `if (crash_irq < 0 && crash_irq != -ENXIO) return dev_err_probe(...)`, and fail the
probe if the request fails. Optional: in the binding, `dependencies: interrupts:
[interrupt-names]`, since the driver looks the IRQ up by name only.

### 6. Low, CONFIRMED: rproc KUnit remap test is not endian-safe (breaks Lesson 4.1)
`drivers/remoteproc/sunxi_rproc_test.c:331`

The test stores `ctx->mock_remap_reg = SUNXI_REMAP_MCU_RAM_BIT` in CPU byte order. The driver
reads it with `readl()` (little-endian) and writes it back with `writel()`. On a big-endian
`COMPILE_TEST` build, which the Kconfig allows, the test reads `0x01000002`, not `0x3`, and
fails. The commit message says reads were made endian-safe, so this one slipped through.

**Fix:** `writel(SUNXI_REMAP_MCU_RAM_BIT, ctx->priv.remap_va);`.

### 7. Low, PLAUSIBLE: unbinding the message box while the rproc holds a channel
`sun55i-msgbox.c:481-486` (devm `chans`), `:563`, `:576-582` (no `suppress_bind_attrs`), `:505-515` (devm reset assert)

`mbox_request_channel()` creates no device link. A sysfs unbind of the message box therefore runs
`mbox_controller_unregister()`, which frees the rproc's channel behind its back. devres then frees
the `chans` array. The rproc keeps `priv->mbox_chan`, so the next kick calls
`mbox_send_message()` on freed memory. The unbind also asserts the resets of all four banks,
including the CPUS bank (`r_ccu`) and the RV bank of a running E906.

The use-after-free is a framework gap that other controllers share. A reviewer will still ask,
because the cover letter says "message box unbind/bind" was tested.

**Fix:** `.suppress_bind_attrs = true` (precedent: `qcom-ipcc.c`). Or state in the cover letter
that unbind was tested with the remoteproc stopped and unbound.

### 8. Low, CONFIRMED: the exported patches and cover letter name the old base
`upstream-remoteproc/v3/v3-0000-cover-letter.patch`

The cover letter says "Rebased on next-20261020" and "On next-20261020: W=1 ... clean", and every
exported patch carries `base-commit: 8f7df005a756` (next-20261020). The branch is on
next-20261026 (`f1ac44c32cba`). The patch IDs of the exported files match the branch, so only the
metadata and text are stale. TODO §5.6a V3-READY already plans a re-export on send day: also
update both cover-letter sentences.

### 9. Nit, CONFIRMED: comments that contradict the code
- `sun55i-msgbox.c:399-402` says "The core also calls shutdown() when startup() failed". In this
  tree `__mbox_bind_client()` calls only `mbox_clean_and_put_channel()` when `startup()` fails;
  `shutdown()` is not called. The `test_and_clear_bit` guard is harmless, but the comment is
  wrong: reword it as defensive, or drop it.
- `sun55i-msgbox.c:136-139` says "send_data() takes the lock". That is true only in tx-done-irq
  mode; in timer mode `send_data()` takes no lock.
- `sun55i_msgbox_test.c:9` says "armed only while the FIFO is full". The driver (and the test at
  `:360`) arms the write IRQ after every send.
- `sunxi_rproc_test.c:479-484` says "runs in hardirq context with the msgbox lock held". v3
  delivers outside the lock. (`UPSTREAM_LESSONS_LEARNED.md` Lesson 3.2b says the same and is also
  out of date.)
- The Kconfig help of `SUNXI_REMOTEPROC_KUNIT_TEST` still describes only da_to_va tests. The suite
  now covers start, prepare/unprepare, kick and rx.

### 10. Nit, CONFIRMED: msgbox KUnit calls `synchronize_irq(0)`
`sun55i_msgbox_test.c` (every test that calls `.shutdown`; the fixture leaves `mbox.irq = 0`) -> `sun55i-msgbox.c:394`

IRQ 0 is a real interrupt on UML and x86 (the timer). The call is harmless, but a test should not
touch a global IRQ. Set the fixture IRQ to a value with no descriptor, or skip the sync when
`mbox->irq <= 0`.

### 11. Nit: other cleanups a maintainer may ask for
- `sun55i_chan_to_route()` (`sun55i-msgbox.c:64-70`) maps an out-of-range index to remote_id 0,
  which is the host's own bank. The case cannot happen, because `n` is derived from the `chan`
  pointer. Drop the branch, or make it `WARN_ON_ONCE`, and drop the test that locks the behaviour
  in.
- The RX handler takes and drops the lock and does two MMIO reads per channel (12 per interrupt),
  and two more reads per message. Read enable & status once per port.
- `peek_data()` reports data but never pushes it, and no client uses it. Jassi may ask why it
  exists.
- `sunxi_rproc_parse_fw()` (`:351-361`) turns every `-EINVAL` into "no table", including a
  malformed table. stm32 does the same.
- `rproc_of_parse_firmware()` exists for `firmware-name`. The default `"riscv-firmware.elf"` is
  very generic.
- `MODULE_AUTHOR("Allwinner Technology Co., Ltd.")` is not a person.
- `unprepare()` uses the async `pm_runtime_put()`. Prefer `pm_runtime_put_sync()`, so the domains
  are off before a following remove or `pm_runtime_disable()`.

---

## Decision risks (settled decisions that may draw pushback, one line each)
- `allwinner,tx-done-irq`: Rob and Krzysztof may call it OS policy. `ti,mbox-send-noirq` is the
  precedent, so cite it up front (the cover letter does).
- `sun55i_readl()`/`sun55i_writel()` with `KUNIT_STATIC_STUB_REDIRECT` puts a test hook on every
  register access when `CONFIG_KUNIT=y`. Jassi may ask for it to go.
- Timer mode's `send_data()` returns 0 for a message it only held. In practice only a FIFO left
  full before `startup()` reaches this path, so Jassi may prefer simply returning `-EBUSY`.
- The direct `remap` register map (D9): Krzysztof is likely to ask for a syscon or R-CCU phandle.
- The rproc takes `RST_BUS_MCU_PUBSRAM` and `CLK_BUS_MCU_PUBSRAM` exclusively and gates them in
  `unprepare()`. If the DSP shares that SRAM, a future DSP driver will conflict.
- The `vram` pool (index 0) is both the DMA pool and a carveout (D10). Mathieu may ask why the
  pool needs a carveout entry.
- The msgbox node is enabled by default, which clocks the DSP and CPUS banks and releases their
  resets on every A523 board.
- `sunxi_rproc.h` and `sun55i-msgbox.h` exist only for KUnit. Bjorn may ask for them to be folded
  into the `.c` files.

## Checked and found OK
- Mailbox lock order. `mbox->lock` is held only around MMIO, never across
  `mbox_chan_received_data()` or `mbox_chan_txdone()`. A client sending from its rx callback
  cannot deadlock, and the TX IRQ cannot deadlock against `send_data()` (which runs under
  `chan->lock` in `msg_submit()`).
- RX drain: bounded at 8, pending bit cleared after the drain, and the level re-fire covers a
  FIFO with more than 8 messages. Delivery order is kept, because genirq serialises a handler.
  `shutdown()` disables the enable bit, then `synchronize_irq()`, before the core clears
  `chan->cl`.
- Timer mode: `last_tx_done()` is `count < 8` (decision). The core polls only while
  `active_req` is set. The held-message path writes exactly once (shown by the KUnit test), and
  `shutdown()` drops a held message. `send_data()` and `last_tx_done()` never run concurrently on
  the same channel.
- IRQ mode, normal path: arm after every send with the latched status cleared first; on
  `-EBUSY` the message stays queued and the IRQ retries it (`tx_tick` with `NO_MSG` resubmits);
  no lost wake-up, per the latched-status behaviour measured in reference §6a.
- TX line refcount (`tx_irq_lock`). `request_irq()` runs only from `startup()` in process context.
  If `startup()` fails, the RX enable is rolled back.
- Msgbox probe: resets are deasserted before the first register write. The IRQ is requested after
  the read enables are zeroed, so an early IRQ does not dereference `chans`. devm teardown runs
  in this order: controller unregister (shutdown, hrtimer cancel), IRQ free, reset assert, clocks.
- Rproc teardown: `rproc_del()`, then `mbox_free_channel()`, then `cancel_work_sync()`. The probe
  error paths release reserved memory and the mailbox. The workqueue is devm-managed and freed
  after `remove()`.
- `rproc_vq_interrupt()` against vdev removal is SRCU-protected in this tree
  (`remoteproc_virtio.c:110-117`, `:191-200`), so the deferred work is safe during stop and
  recovery.
- Crash IRQ: `IRQF_NO_AUTOEN` plus the atomic ARMED bit keeps `enable_irq`/`disable_irq` balanced
  across the crash handler, `stop()` and `start()`. `disable_irq()` is not called under a lock.
- `prepare()`/`unprepare()`: the error unwind is complete and in reverse order. Carveouts are
  re-registered on every boot, and `rproc_resource_cleanup()` frees them.
- `da_to_va()`: overflow and zero-length checks; no straddling of the adjacent windows; addresses
  that miss the ATT return NULL and fall through to the core's carveout lookup.
- Kick payloads are `static const`, so they outlive the queued pointer.
- Bindings: reg, clocks, resets, interrupts and memory-region are positional lists. The
  `if`/`then` makes tx-done-irq require 4 interrupts. The driver maps by index, in binding order.
- DTS: every clock, reset and power-domain macro exists (`ccu-sun55i-a523-mcu.c`,
  `ccu-sun55i-a523-r.c`, `allwinner,sun55i-a523-ppu.h`). SPI 0/181/174/205 match reference §1.
  No unit-address overlaps; the nodes are in address order.
- `devm_pm_domain_attach_list()` with 2 domains attaches with `DL_FLAG_PM_RUNTIME` links, and the
  platform bus provides runtime ops, so `pm_runtime_resume_and_get()` powers both domains.
- KUnit: 26 msgbox and 15 rproc cases, matching the commit messages. The msgbox mock models
  pop, push and W1C, and the tests would fail if the drain order, held-message or arm-after-send
  behaviour regressed. Both suites import `EXPORTED_FOR_KUNIT_TESTING`.
- The exported `upstream-remoteproc/v3/*.patch` files are patch-id identical to the branch.
