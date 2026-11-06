# Upstream Review Feedback & Mailing List Archive: v3

- **Submission Date**: Sun, 1 Nov 2026 18:57 UTC
- **Lore Master Thread**: https://lore.kernel.org/linux-sunxi/20261101185735.762559-1-tcmichals@gmail.com/
- **Sashiko**: https://sashiko.dev/#/patchset/20261101185735.762559-1-tcmichals@gmail.com
- **Base**: next-20261030 (base-commit 7649ba73f9e7e1e04d0b46213e39c79ae09dd55c)
- **Total Review Emails Received**: 8 (2 Krzysztof Kozlowski, 2 Uwe Kleine-König, 4 Sashiko bot; thread has 16 messages)
- **Raw Email Archive Directory**: `upstream-remoteproc/v3/emails/`
- **No replies** on 5/7 (DTS) and 6/7 (mailbox KUnit). Sashiko had no findings on 3/7.

Section 2 is the email text copied by script from the `.eml` files (EMAIL_INGEST_GUIDE rule 1:
verbatim, never paraphrased). Section 1 is the resolution ledger for v4.

---

## 1. Resolution ledger (v4)

Status: **Decided** = the owner chose; **Planned** = fix follows from a decision or has nothing to
decide; **Open** = needs the owner's yes/no. Every High/Medium finding is fixed or put to the
owner; none is closed as "left as is".

| ID | Patch | From | Finding | Repeat? | v4 plan | Status |
|----|-------|------|---------|---------|---------|--------|
| V3-K1 | 1/7 | Krzysztof | "Sashiko comment looks valid, rest seems ok" — `allwinner,tx-done-irq` is driver policy | no | Drop the property; TX done by the write interrupt only; delete timer/poll mode | Decided |
| V3-S1 | 1/7 | Sashiko (Medium) | `allwinner,tx-done-irq` encodes driver policy | no | Same as V3-K1 | Decided |
| V3-S2 | 2/7 | Sashiko (High) | `-EBUSY` from `send_data()` in IRQ mode leaves the message in `chan->msg_data[]` with `active_req == NULL`: UAF after a blocking timeout, double `tx_prepare`, premature txdone | no | Hold the message (as timer mode did) and return 0; the TX IRQ handler writes it once a slot is free | Planned |
| V3-S3 | 2/7 | Sashiko (Medium) | Lock dropped between FIFO write and clearing/arming the latched write-IRQ status: stale pending bit completes a message while FIFO is full | no | Clear status + arm under the same `mbox->lock` section as the FIFO write | Planned |
| V3-S4 | 2/7 | Sashiko (High) | `shutdown()` on a shared route: `synchronize_irq()` does not stop a second level-triggered TX IRQ re-armed by `send_data()` from racing `mbox_free_channel()` | no | Do not arm once `shutdown()` has started (check a per-channel flag under `mbox->lock` in `send_data()`) | Planned |
| V3-S5 | 4/7 | Sashiko (High) | `sunxi_rproc_vq_work()` walks `rproc->notifyids` unlocked; races `rproc_resource_cleanup()` (UAF) | **yes, v2** | Take `rproc->lock` and check `RPROC_RUNNING` in the work, as stm32/imx_dsp do; no `cancel_work_sync()` in `stop()` (stop runs under `rproc->lock` → deadlock) | Planned |
| V3-S6 | 4/7 | Sashiko (High) | `stop()` does not `synchronize_irq()` when the crash handler already cleared ARMED: unbalanced enable, handler reports crash after restart | **yes, v2** | Remove the crash interrupt (no line on A523, none in vendor BSP, never tested); crash dump stays. Fixes V3-K2c too | Open |
| V3-K2a | 3/7 | Krzysztof | `memory-region` `minItems: 1` — "same comments as for v2" | **yes, v2** | Fixed list (drop `minItems`), or a hardware reason in the binding for each optional entry | Open: are dram and trace always used? |
| V3-K2b | 3/7 | Krzysztof | `memory-region-names` `minItems: 1` | **yes, v2** | Same as V3-K2a | Open |
| V3-K2c | 3/7 | Krzysztof | `interrupts` (crash) — "same comments as for v2" | **yes, v2** | Drop from the binding with V3-S6 | Open |
| V3-S7 | 7/7 | Sashiko (Medium) | `SUNXI_REMOTEPROC_KUNIT_TEST` needs `depends on PM` (`power.usage_count`, PM-domain failure test) | no | Add `PM` to `depends on`; add a PM=n build to pre-send checks | Planned |
| V3-S8 | 7/7 | Sashiko (Low) | `use_mmio_mocks()` casts lack `__force` (sparse) | no | Add `__force`; add `C=1` sparse to pre-send checks | Planned |
| V3-U1 | 2/7, 4/7 | Uwe Kleine-König | Do not include `<linux/mod_devicetable.h>`; `<linux/platform_device.h>` provides `of_device_id` | no | Remove the include from both drivers. Do not add `<linux/device-id/of.h>`: `cubie-linux-7.1` does not have it, and the line removal builds on both trees (7.1 gets it through `<linux/of.h>`) | Fixed: 7.1 `9a5f36c0628d`, W=1 clean on 7.1 and next-20261105 |

### Process changes (why these came back)
1. Every High/Medium finding is fixed or put to the owner as a yes/no, never closed in a TODO row.
2. A precedent argument cites all peer drivers, including those that do it differently.
3. Earlier findings are re-checked against the new code by re-running the failure scenario.
4. Correct the `minItems: 1` rule in UPSTREAM_LESSONS_LEARNED.md, `.agents/rules/upstream-patch-submissions.md` and v3/review/REVIEW_ANSWER_KEY.md.
5. Add a concurrency pass through the subsystem core (blocking timeout, unbind during traffic, a second IRQ after `synchronize_irq()`).
6. Add `C=1` sparse and a `CONFIG_PM=n` build to the pre-send checks.

---

## 2. Verbatim review emails

### 1. Krzysztof Kozlowski <krzk@kernel.org> — Re: [PATCH v3 1/7] dt-bindings: mailbox: Add Allwinner A523 message box
- **Date**: Mon, 02 Nov 2026 11:17:39 +0100
- **Message-ID**: `<20261102-hot-glistening-cassowary-c1da4e@quoll>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/20261102-hot-glistening-cassowary-c1da4e@quoll/
- **Raw Email File**: `v3/emails/13_20261102-hot-glistening-cassowary-c1da4e_quoll.eml`

#### Verbatim text
````text
On Sun, Nov 01, 2026 at 12:57:29PM -0600, Tim Michals wrote:
> The Allwinner A523, A527 and T527 (sun55i) message box connects the
> Cortex-A55 host with the HiFi4 DSP, the CPUS power management core and
> the XuanTie E906 RISC-V core. Each processor has its own register bank.
> A sender writes a message into the receiving processor's bank, and a
> channel has an 8-entry FIFO in each direction.
> 
> The host bank has 12 channels, four for each remote processor. The
> binding lists all four banks, since the host writes to and reads the
> FIFO status of the remote banks, and lists the bank interrupts in the
> same order: the host bank read interrupt and the write interrupts of
> the three remote banks, which every A523 has. Each bank has its own bus
> clock and reset, and an unclocked bank drops writes, so all four are
> described. The optional allwinner,tx-done-irq property selects TX
> completion by the receiver bank's write interrupt instead of polling.
>

Sashiko comment looks valid, rest seems ok.

Best regards,
Krzysztof
````

---

### 2. Krzysztof Kozlowski <krzk@kernel.org> — Re: [PATCH v3 3/7] dt-bindings: remoteproc: Add Allwinner A523 RISC-V remote processor
- **Date**: Mon, 02 Nov 2026 11:19:54 +0100
- **Message-ID**: `<20261102-warping-victorious-shrew-7cee1e@quoll>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/20261102-warping-victorious-shrew-7cee1e@quoll/
- **Raw Email File**: `v3/emails/14_20261102-warping-victorious-shrew-7cee1e_quoll.eml`

#### Verbatim text
````text
On Sun, Nov 01, 2026 at 12:57:31PM -0600, Tim Michals wrote:
> +  mboxes:
> +    description:
> +      Message box channel to the RISC-V core. Each channel has a FIFO in
> +      each direction, so it carries notifications both ways.
> +    maxItems: 1
> +
> +  firmware-name:
> +    maxItems: 1
> +
> +  memory-region:
> +    minItems: 1


Here

> +    items:
> +      - description: Virtio vring and buffer pool, a shared-dma-pool
> +      - description: Firmware code and data in DRAM
> +      - description: Trace buffer
> +
> +  memory-region-names:
> +    minItems: 1

here

> +    items:
> +      - const: vram
> +      - const: dram
> +      - const: trace
> +
> +  interrupts:
> +    maxItems: 1

and here, same comments as for v2.

> +    description:
> +      Interrupt the RISC-V core raises on a fatal error. The A523 has no
> +      dedicated line for this, so it is present only where the system
> +      integration provides one.

Best regards,
Krzysztof
````

---

### 3. sashiko-bot@kernel.org — Re: [PATCH v3 1/7] dt-bindings: mailbox: Add Allwinner A523 message box
- **Date**: Sun, 01 Nov 2026 19:06:22 +0000
- **Message-ID**: `<sashiko-outbox-192575@kernel.org>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/sashiko-outbox-192575@kernel.org/
- **Raw Email File**: `v3/emails/09_sashiko-outbox-192575_kernel.org.eml`

#### Verbatim text
````text
Thank you for your contribution! Sashiko AI review found 1 potential issue(s) to consider:
- [Medium] dt-bindings: mailbox: sun55i-a523: allwinner,tx-done-irq encodes driver policy
--

commit 84f2f43ac2a1769e84ee3a5f62703aa7b08889d3
Author: Tim Michals <tcmichals@gmail.com>

dt-bindings: mailbox: Add Allwinner A523 message box

This commit adds the Devicetree binding schema for the Allwinner A523, A527,
and T527 (sun55i) message box controller, describing the four processor
register banks, clocks, resets, interrupts, and an optional
allwinner,tx-done-irq property.

> diff --git a/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml b/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml
> new file mode 100644
> index 0000000000000..47b6fe304dbc8
> --- /dev/null
> +++ b/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml

[ ... ]

> +  interrupts:
> +    items:
> +      - description: Cortex-A55 bank read interrupt, messages for the host
> +      - description: HiFi4 DSP bank write interrupt for the host
> +      - description: CPUS bank write interrupt for the host
> +      - description: XuanTie RISC-V bank write interrupt for the host
> +
> +  interrupt-names:
> +    items:
> +      - const: arm
> +      - const: dsp
> +      - const: cpus
> +      - const: rv
> +
> +  allwinner,tx-done-irq:
> +    type: boolean
> +    description:
> +      Signal TX done with the write interrupt of the receiver's bank instead
> +      of polling. A message is done once the receiver's FIFO has room for the
> +      next one either way; the write interrupt is raised while the FIFO has
> +      room. Without this property a full FIFO is polled. Received messages
> +      use the host bank read interrupt either way.

[Severity: Medium]
Since the schema unconditionally requires all four hardware interrupt lines
(arm, dsp, cpus, and rv) in interrupts and interrupt-names, and every A523
SoC wires the three remote bank write interrupts to the GIC, this boolean
property appears to only select between interrupt-driven and polled TX
completion in the driver rather than describing a hardware difference.

Given that Devicetree bindings should describe hardware capabilities and
wiring rather than OS driver runtime policy, should allwinner,tx-done-irq be
dropped from the binding so the driver decides how to handle TX completion?

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20261101185735.762559-1-tcmichals@gmail.com?part=1
````

---

### 4. sashiko-bot@kernel.org — Re: [PATCH v3 2/7] mailbox: sun55i: Add Allwinner A523 message box driver
- **Date**: Sun, 01 Nov 2026 19:17:22 +0000
- **Message-ID**: `<sashiko-outbox-192582@kernel.org>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/sashiko-outbox-192582@kernel.org/
- **Raw Email File**: `v3/emails/11_sashiko-outbox-192582_kernel.org.eml`

#### Verbatim text
````text
Thank you for your contribution! Sashiko AI review found 3 potential issue(s) to consider:
- [High] mailbox: sun55i: TX IRQ race in sun55i_msgbox_shutdown() on shared route
- [High] mailbox: sun55i: UAF and race returning -EBUSY in sun55i_msgbox_send_data()
- [Medium] mailbox: sun55i: lock drop race enabling TX IRQ in sun55i_msgbox_send_data()
--

commit e3cf2bfaf2bd6f778a72431227fd527e0aef2173
Author: Tim Michals <tcmichals@gmail.com>

mailbox: sun55i: Add Allwinner A523 message box driver

This commit adds a mailbox controller driver for the Allwinner sun55i (A523)
message box, supporting 12 hardware FIFO channels between the Cortex-A55 host
and the CPUS, HiFi4 DSP, and XuanTie E906 processors with either polled or
write-IRQ TX completion.

> diff --git a/drivers/mailbox/sun55i-msgbox.c b/drivers/mailbox/sun55i-msgbox.c
> new file mode 100644
> index 0000000000000..1618c16219fc2
> --- /dev/null
> +++ b/drivers/mailbox/sun55i-msgbox.c
> @@ -0,0 +1,613 @@

[ ... ]

> +static void sun55i_msgbox_tx_irq_put(struct sun55i_msgbox *mbox, int local_n)
> +{
> +	struct sun55i_tx_irq *tx = &mbox->tx_irqs[local_n];
> +
> +	mutex_lock(&mbox->tx_irq_lock);
> +	if (!--tx->users) {
> +		free_irq(tx->irq, tx);
> +	} else {
> +		/* The line stays in use: let a running handler finish with us */
> +		synchronize_irq(tx->irq);
> +	}
> +	mutex_unlock(&mbox->tx_irq_lock);
> +}
> +
> +static int sun55i_msgbox_send_data(struct mbox_chan *chan, void *data)
> +{
> +	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
> +	int n = chan - mbox->controller.chans;
> +	int local_n, p, remote_id, remote_n;
> +	unsigned long flags;
> +	u32 msg = 0;
> +	bool full;
> +
> +	if (data)
> +		memcpy(&msg, data, sizeof(msg));
> +
> +	sun55i_msgbox_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);
> +
> +	/*
> +	 * Under the lock: in timer mode the poll timer may write a held
> +	 * message into the same FIFO from another CPU (last_tx_done()).
> +	 */
> +	spin_lock_irqsave(&mbox->lock, flags);
> +
> +	/*
> +	 * The core calls send_data() only with no request active, so a message
> +	 * still held here is one it gave up on (a blocking send timed out):
> +	 * drop it rather than send it after this newer one.
> +	 */
> +	__clear_bit(n, &mbox->tx_held);
> +
> +	/*
> +	 * Writing a full FIFO corrupts its count, so never do it. In write IRQ
> +	 * mode the IRQ armed below retries the message once a slot is free. In
> +	 * timer mode the core polls only a channel whose send succeeded, so
> +	 * hold the message and let last_tx_done() write it.
> +	 */
> +	full = sun55i_msgbox_tx_count(mbox, remote_id, remote_n, p) >= SUN55I_FIFO_MAX;
> +	if (!full) {
> +		sun55i_msgbox_writel(msg, mbox->regs[remote_id] +
> +				     SUN55I_MSGBOX_MSG_FIFO(remote_n, p));
> +	} else if (!mbox->tx_done_irq) {
> +		mbox->tx_held_msg[n] = msg;
> +		__set_bit(n, &mbox->tx_held);
> +	}
> +
> +	spin_unlock_irqrestore(&mbox->lock, flags);

[Severity: Medium]
Can dropping mbox->lock here before calling sun55i_msgbox_tx_irq_enable()
cause a premature TX completion when a previous blocking send timed out?

If an earlier blocking send timed out in mbox_send_message() ->
tx_tick(chan, -ETIME), SUN55I_MSGBOX_WR_IRQ_EN_BIT(p) stays enabled in the
remote bank. When the remote processor later reads a message and frees a slot,
SUN55I_MSGBOX_WR_IRQ_PEND_BIT(p) is latched in
SUN55I_MSGBOX_WRITE_IRQ_STATUS(remote_n).

When the next sun55i_msgbox_send_data() writes to SUN55I_MSGBOX_MSG_FIFO and
fills the FIFO to SUN55I_FIFO_MAX, releasing mbox->lock before clearing the
latched status bit in sun55i_msgbox_tx_irq_enable() opens a window where
sun55i_msgbox_tx_irq() on another CPU can acquire mbox->lock, see both
WR_IRQ_EN_BIT(p) and the stale latched WR_IRQ_PEND_BIT(p) set, and call
mbox_chan_txdone() to complete the new message while the FIFO is still full.

Should the write IRQ status clear and enable in sun55i_msgbox_tx_irq_enable()
be performed under the same mbox->lock critical section as the FIFO write?

> +
> +	/*
> +	 * A kick is a doorbell, the data is in shared memory: it is done once
> +	 * the FIFO accepted it and has room for the next one. The write IRQ
> +	 * reports exactly that, at once or when the receiver reads a message.
> +	 */
> +	if (mbox->tx_done_irq) {
> +		sun55i_msgbox_tx_irq_enable(mbox, remote_id, remote_n, p, true);
> +		return full ? -EBUSY : 0;
> +	}

[Severity: High]
What happens in write-IRQ mode (mbox->tx_done_irq) when the hardware FIFO is
full and sun55i_msgbox_send_data() arms the write IRQ and returns -EBUSY?

In drivers/mailbox/mailbox.c, msg_submit() only sets chan->active_req = data
and decrements chan->msg_count when send_data() returns 0:

drivers/mailbox/mailbox.c:msg_submit() {
	...
	if (chan->cl->tx_prepare)
		chan->cl->tx_prepare(chan->cl, data);
	/* Try to submit a message to the MBOX controller */
	err = chan->mbox->ops->send_data(chan, data);
	if (!err) {
		chan->active_req = data;
		chan->msg_count--;
	}
	...
}

When send_data() returns -EBUSY, chan->active_req remains NULL and the
caller's data pointer stays in chan->msg_data[]. If a blocking send in
mbox_send_message() then times out in wait_for_completion_timeout(), it calls
tx_tick(chan, -ETIME):

drivers/mailbox/mailbox.c:tx_tick() {
	...
	spin_lock_irqsave(&chan->lock, flags);
	mssg = chan->active_req;
	chan->active_req = NULL;
	spin_unlock_irqrestore(&chan->lock, flags);

	/* Submit next message */
	msg_submit(chan);

	if (!mssg)
		return;
	...
}

Because chan->active_req is NULL, tx_tick() does not remove the timed-out
message from chan->msg_data[]. Instead, mbox_send_message() returns -ETIME to
the caller while its message pointer (often stack-allocated in blocking mode)
is still queued in chan->msg_data[] with the write IRQ enabled. When the
remote processor later drains a FIFO slot, sun55i_msgbox_tx_irq() ->
mbox_chan_txdone() -> tx_tick() -> msg_submit() will call cl->tx_prepare(),
sun55i_msgbox_send_data() (which runs memcpy(&msg, data, sizeof(msg))), and
cl->tx_done() on the dangling pointer.

Even without a timeout, while chan->active_req is NULL and the first message
waits in chan->msg_data[] for the write IRQ, a second mbox_send_message() call
on another CPU can run msg_submit() just as a FIFO slot frees: CPU 0 sees
chan->active_req == NULL, calls cl->tx_prepare() a second time on the first
message, writes it into the single free FIFO slot (refilling the FIFO), and
sets chan->active_req. The concurrent sun55i_msgbox_tx_irq() on CPU 1 then
calls mbox_chan_txdone() and prematurely completes the message while the FIFO
is full.

Should write-IRQ mode also buffer the message in mbox->tx_held_msg[n] and
return 0 when the FIFO is full, writing the held message from
sun55i_msgbox_tx_irq() once a slot is free?

[ ... ]

> +static void sun55i_msgbox_shutdown(struct mbox_chan *chan)
> +{
> +	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
> +	void __iomem *base = mbox->regs[SUN55I_PROC_ARM];
> +	int n = chan - mbox->controller.chans;
> +	int local_n, p, remote_id, remote_n;
> +	u32 stale[SUN55I_FIFO_MAX];
> +	unsigned long flags;
> +	u32 val;
> +
> +	sun55i_msgbox_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);
> +
> +	/* Disable the receive IRQ; a message held for a full FIFO is dropped */
> +	spin_lock_irqsave(&mbox->lock, flags);
> +	val = sun55i_msgbox_readl(base + SUN55I_MSGBOX_READ_IRQ_ENABLE(local_n));
> +	val &= ~SUN55I_MSGBOX_RD_IRQ_EN_BIT(p);
> +	sun55i_msgbox_writel(val, base + SUN55I_MSGBOX_READ_IRQ_ENABLE(local_n));
> +	__clear_bit(n, &mbox->tx_held);
> +	spin_unlock_irqrestore(&mbox->lock, flags);
> +
> +	/* The handler delivers outside the lock: let it finish with this channel */
> +	synchronize_irq(mbox->irq);
> +
> +	/* Release the line only if this channel's startup() got it */
> +	if (test_and_clear_bit(n, &mbox->tx_irq_held)) {
> +		sun55i_msgbox_tx_irq_enable(mbox, remote_id, remote_n, p, false);
> +		sun55i_msgbox_tx_irq_put(mbox, local_n);
> +		/*
> +		 * A TX-done handler that was already running submits the next
> +		 * message through send_data(), which arms the IRQ again. It has
> +		 * finished now (free_irq() or synchronize_irq() above), so
> +		 * disarm once more.
> +		 */
> +		sun55i_msgbox_tx_irq_enable(mbox, remote_id, remote_n, p, false);
> +	}

[Severity: High]
Can a second sun55i_msgbox_tx_irq() invocation still race with channel
teardown here when another channel on the same route remains active
(tx->users > 1)?

In mbox_free_channel(), chan->mbox->ops->shutdown(chan) runs before chan->cl
is cleared, and queued messages may still be present in chan->msg_data[]. When
tx->users > 1 on local_n, sun55i_msgbox_tx_irq_put() only calls
synchronize_irq(tx->irq) and leaves tx->irq enabled at the interrupt
controller.

If an in-flight sun55i_msgbox_tx_irq() was already past mbox->lock on another
CPU, it calls:

sun55i_msgbox_tx_irq()
  mbox_chan_txdone()
    tx_tick()
      msg_submit()
        sun55i_msgbox_send_data()
          sun55i_msgbox_tx_irq_enable(mbox, remote_id, remote_n, p, true)

Because the write IRQ is level-triggered whenever at least one FIFO slot is
free, re-enabling SUN55I_MSGBOX_WR_IRQ_EN_BIT(p) in sun55i_msgbox_send_data()
can immediately assert tx->irq again as soon as the first handler returns.
Waiters in sun55i_msgbox_tx_irq_put()'s synchronize_irq(tx->irq) only wait for
that first handler invocation to complete, so a second sun55i_msgbox_tx_irq()
can fire before the second sun55i_msgbox_tx_irq_enable(..., false) runs and
execute mbox_chan_txdone() -> tx_tick() concurrently with mbox_free_channel()
setting chan->cl = NULL (or re-arm the write IRQ again if more messages were
queued).

Would checking test_bit(n, &mbox->tx_irq_held) under mbox->lock before arming
the write IRQ in sun55i_msgbox_send_data() avoid re-enabling the IRQ after
sun55i_msgbox_shutdown() has begun?

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20261101185735.762559-1-tcmichals@gmail.com?part=2
````

---

### 5. sashiko-bot@kernel.org — Re: [PATCH v3 4/7] remoteproc: sunxi: Add Allwinner A523 XuanTie E906 driver
- **Date**: Sun, 01 Nov 2026 19:24:20 +0000
- **Message-ID**: `<sashiko-outbox-192595@kernel.org>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/sashiko-outbox-192595@kernel.org/
- **Raw Email File**: `v3/emails/12_sashiko-outbox-192595_kernel.org.eml`

#### Verbatim text
````text
Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:
- [High] remoteproc: sunxi: missing IRQ sync in sunxi_rproc_stop() races crash handler
- [High] remoteproc: sunxi: race and UAF in sunxi_rproc_vq_work() on notifyids
--

commit f895edd61f6cf2e80a5341959b347c747139fe48
Author: Tim Michals <tcmichals@gmail.com>

remoteproc: sunxi: Add Allwinner A523 XuanTie E906 driver

This commit adds a remoteproc driver for the XuanTie E906 RISC-V core on
Allwinner A523, A527, and T527 SoCs, handling power domains, clocks, resets,
SRAM mappings, memory-region carveouts, mailbox notifications, and crash
reporting.

> diff --git a/drivers/remoteproc/sunxi_rproc.c b/drivers/remoteproc/sunxi_rproc.c
> new file mode 100644
> index 0000000000000..1cee7110f0b8c
> --- /dev/null
> +++ b/drivers/remoteproc/sunxi_rproc.c
> @@ -0,0 +1,855 @@

[ ... ]

> +VISIBLE_IF_KUNIT
> +int sunxi_rproc_vq_notify(int id, void *ptr, void *data)
> +{
> +	KUNIT_STATIC_STUB_REDIRECT(sunxi_rproc_vq_notify, id, ptr, data);
> +	rproc_vq_interrupt(data, id);
> +	return 0;
> +}
> +EXPORT_SYMBOL_IF_KUNIT(sunxi_rproc_vq_notify);
> +
> +/* One channel carries all notifications: check every vring */
> +VISIBLE_IF_KUNIT
> +void sunxi_rproc_vq_work(struct work_struct *work)
> +{
> +	struct sunxi_rproc *priv = container_of(work, struct sunxi_rproc, vq_work);
> +
> +	idr_for_each(&priv->rproc->notifyids, sunxi_rproc_vq_notify, priv->rproc);
> +}
> +EXPORT_SYMBOL_IF_KUNIT(sunxi_rproc_vq_work);

[Severity: High]
As reported by Sashiko on v2 [1], in drivers/remoteproc/sunxi_rproc.c, can
sunxi_rproc_vq_work() race with rproc_resource_cleanup() during
rproc_shutdown(), rproc_boot_recovery(), or sunxi_rproc_remove() ->
rproc_del() and trigger a use-after-free?

Because priv->mbox_chan stays active across remoteproc stops, crash
recoveries, and throughout rproc_del() in sunxi_rproc_remove(), and neither
sunxi_rproc_stop() nor sunxi_rproc_unprepare() cancels priv->vq_work,
sunxi_rproc_vq_work() can run concurrently with rproc_resource_cleanup().

In sunxi_rproc_vq_work(), idr_for_each() iterates priv->rproc->notifyids
without holding priv->rproc->lock (unlike stm32_rproc_mb_vq_work() or
imx_dsp_rproc_vq_work()) and without RCU read-side protection (which cannot
be held across sunxi_rproc_vq_notify() because rproc_vq_interrupt() invokes
virtqueue callbacks that can sleep):

  lib/idr.c:idr_for_each()
    radix_tree_for_each_slot(slot, &idr->idr_rt, &iter, 0)
      fn(id, rcu_dereference_raw(*slot), data)

While sunxi_rproc_vq_notify() is executing, a concurrent teardown runs:

  rproc_resource_cleanup()
    platform_device_unregister(rvdev->pdev)
      rproc_virtio_remove()
        rproc_free_vring()
          idr_remove(&rproc->notifyids, rvring->notifyid)

If removing the vring frees the underlying struct radix_tree_node via
call_rcu(), the grace period can finish while sunxi_rproc_vq_notify() sleeps,
leaving radix_tree_next_slot() in idr_for_each() to dereference a freed slot
pointer.

In addition, although __rproc_virtio_del_vqs() clears rvring->vq and runs
synchronize_srcu(&rproc->vq_srcu) during rproc_stop_subdevices(), the rvring
entry stays in rproc->notifyids until platform_device_unregister(rvdev->pdev)
removes it and immediately frees the devm-allocated struct rproc_vdev
without another synchronize_srcu(&rproc->vq_srcu). If rproc_vq_interrupt()
looks up rvring in rproc->notifyids just before idr_remove(), it can
dereference rvring->vq after rvdev has been freed.

Should sunxi_rproc_vq_work() hold mutex_lock(&priv->rproc->lock) around
idr_for_each(), or should the driver synchronize and stop queuing vq_work
before rproc_resource_cleanup() tears down the vrings?

> +
> +/*
> + * Mailbox rx callbacks run in atomic context, but the virtio/rpmsg receive
> + * path takes mutexes, so the vring notifications are handled from a work item.
> + */
> +VISIBLE_IF_KUNIT
> +void sunxi_rproc_mb_rx_callback(struct mbox_client *cl, void *data)
> +{
> +	struct sunxi_rproc *priv = container_of(cl, struct sunxi_rproc, cl);
> +
> +	queue_work(priv->workqueue, &priv->vq_work);
> +}
> +EXPORT_SYMBOL_IF_KUNIT(sunxi_rproc_mb_rx_callback);
> +
> +VISIBLE_IF_KUNIT
> +irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
> +{
> +	struct sunxi_rproc *priv = data;
> +
> +	/*
> +	 * Stays masked until the next start(); recovery runs stop() first. A
> +	 * stop() that already disarmed it has disabled the line itself.
> +	 */
> +	if (test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
> +		disable_irq_nosync(irq);
> +
> +	rproc_report_crash(priv->rproc, RPROC_FATAL_ERROR);
> +
> +	return IRQ_HANDLED;
> +}
> +EXPORT_SYMBOL_IF_KUNIT(sunxi_rproc_crash_handler);

[ ... ]

> +VISIBLE_IF_KUNIT
> +int sunxi_rproc_start(struct rproc *rproc)
> +{
> +	struct sunxi_rproc *priv = rproc->priv;
> +	const struct sunxi_rproc_cfg *cfg = priv->cfg;
> +	int ret;
> +
> +	dev_dbg(priv->dev, "Starting %s core at entry 0x%llx\n",
> +		cfg->name, (u64)rproc->bootaddr);
> +
> +	if (rproc->bootaddr > U32_MAX)
> +		return -EINVAL;
> +
> +	/* Program the reset vector and flush it before releasing the core */
> +	sunxi_rproc_writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);
> +	sunxi_rproc_readl(priv->cfg_va + cfg->boot_reg_offset);
> +
> +	ret = reset_control_deassert(priv->rst_core);
> +	if (ret) {
> +		dev_err(priv->dev, "failed to release core reset: %d\n", ret);
> +		return ret;
> +	}
> +
> +	if (priv->crash_irq > 0 &&
> +	    !test_and_set_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
> +		enable_irq(priv->crash_irq);
> +
> +	return 0;
> +}
> +EXPORT_SYMBOL_IF_KUNIT(sunxi_rproc_start);
> +
> +static int sunxi_rproc_stop(struct rproc *rproc)
> +{
> +	struct sunxi_rproc *priv = rproc->priv;
> +	const struct sunxi_rproc_cfg *cfg = priv->cfg;
> +
> +	dev_dbg(priv->dev, "Halting %s core...\n", cfg->name);
> +
> +	/* Assert reset so the core stops executing */
> +	reset_control_assert(priv->rst_core);
> +
> +	if (priv->crash_irq > 0 &&
> +	    test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
> +		disable_irq(priv->crash_irq);
> +
> +	return 0;
> +}

[Severity: High]
As reported by Sashiko on v2 [2], in drivers/remoteproc/sunxi_rproc.c, what
happens if sunxi_rproc_stop() runs right after sunxi_rproc_crash_handler()
clears SUNXI_RPROC_CRASH_IRQ_ARMED, while the threaded handler is still
executing?

Because sunxi_rproc_crash_handler() is a preemptible IRQ thread, it can clear
SUNXI_RPROC_CRASH_IRQ_ARMED and be preempted before calling
disable_irq_nosync(irq) or rproc_report_crash(priv->rproc, RPROC_FATAL_ERROR):

  CPU0 (IRQ thread)                    CPU1 (rproc_shutdown / recovery)
  sunxi_rproc_crash_handler()
    test_and_clear_bit(
      SUNXI_RPROC_CRASH_IRQ_ARMED,
      &priv->flags) -> true
                                       sunxi_rproc_stop()
                                         test_and_clear_bit(
                                           SUNXI_RPROC_CRASH_IRQ_ARMED,
                                           &priv->flags) -> false
                                         /* returns without waiting */

If a subsequent rproc_boot() calls sunxi_rproc_start() before CPU0 reaches
disable_irq_nosync(irq), sunxi_rproc_start() sets SUNXI_RPROC_CRASH_IRQ_ARMED
and calls enable_irq(priv->crash_irq) while the IRQ disable depth is still 0,
triggering an unbalanced enable warning in __enable_irq(). When CPU0 then
runs disable_irq_nosync(irq), the IRQ is left disabled while
SUNXI_RPROC_CRASH_IRQ_ARMED is set, and the next sunxi_rproc_stop() will
increment the disable depth to 2, leaving the crash IRQ permanently masked on
later boots.

Furthermore, because sunxi_rproc_stop() does not call
synchronize_irq(priv->crash_irq) when test_and_clear_bit() returns false, the
in-flight sunxi_rproc_crash_handler() thread can call rproc_report_crash()
after the stop (and a subsequent restart) has already completed.

Should sunxi_rproc_stop() still call synchronize_irq(priv->crash_irq) when
priv->crash_irq > 0 and SUNXI_RPROC_CRASH_IRQ_ARMED was already cleared by
the crash handler?

[1] https://lore.kernel.org/r/20260927003338.CB0E21F000FF@smtp.kernel.org
[2] https://lore.kernel.org/r/20260927003338.CB0E21F000FF@smtp.kernel.org

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20261101185735.762559-1-tcmichals@gmail.com?part=4
````

---

### 6. sashiko-bot@kernel.org — Re: [PATCH v3 7/7] remoteproc: sunxi: Add KUnit tests
- **Date**: Sun, 01 Nov 2026 19:13:03 +0000
- **Message-ID**: `<sashiko-outbox-192578@kernel.org>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/sashiko-outbox-192578@kernel.org/
- **Raw Email File**: `v3/emails/10_sashiko-outbox-192578_kernel.org.eml`

#### Verbatim text
````text
Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:
- [Medium] remoteproc: sunxi: missing CONFIG_PM dependency in KUnit test
- [Low] remoteproc: sunxi: missing __force on __iomem casts in use_mmio_mocks()
--

commit 9db9877d3095c9678cf19b6cc5cf48fbc1c0e95d
Author: Tim Michals <tcmichals@gmail.com>

remoteproc: sunxi: Add KUnit tests

This commit adds 26 KUnit tests for the Allwinner sunxi remoteproc driver,
covering address translation, lifecycle callbacks, crash handling, firmware
parsing, and mailbox notifications using KUnit static stubs and mock
providers.

> diff --git a/drivers/remoteproc/Kconfig b/drivers/remoteproc/Kconfig
> index d2929a3290c3f..6516e272689e4 100644
> --- a/drivers/remoteproc/Kconfig
> +++ b/drivers/remoteproc/Kconfig
> @@ -348,6 +348,21 @@ config SUNXI_REMOTEPROC
>  	  To compile this driver as a module, choose M here: the module will
>  	  be called sunxi_rproc.
>  
> +config SUNXI_REMOTEPROC_KUNIT_TEST
> +	tristate "KUnit tests for Allwinner sunxi remoteproc" if !KUNIT_ALL_TESTS
> +	depends on SUNXI_REMOTEPROC && KUNIT && COMMON_CLK && RESET_CONTROLLER
> +	default KUNIT_ALL_TESTS

[Severity: Medium]
Should SUNXI_REMOTEPROC_KUNIT_TEST also depend on PM?

In include/linux/pm.h, the usage_count member of struct dev_pm_info is only
defined when CONFIG_PM is enabled:

    #ifdef CONFIG_PM
        ...
        atomic_t        usage_count;

When CONFIG_PM is disabled, expect_powered_off(),
test_prepare_unprepare_order(), and test_prepare_power_up_failure() in
drivers/remoteproc/sunxi_rproc_kunit.c fail to compile because they read
dev->power.usage_count unconditionally.

In addition, test_prepare_power_up_failure() relies on dev_pm_domain_set()
and pm_runtime_enable() so that sunxi_rproc_prepare() ->
pm_runtime_resume_and_get() invokes mock_failing_pm_domain.ops.runtime_resume
and returns -EIO. When CONFIG_PM is disabled, those helpers are no-op stubs
and pm_runtime_resume_and_get() returns 0 instead of -EIO.

[ ... ]

> diff --git a/drivers/remoteproc/sunxi_rproc_kunit.c b/drivers/remoteproc/sunxi_rproc_kunit.c
> new file mode 100644
> index 0000000000000..0931564dc22bb
> --- /dev/null
> +++ b/drivers/remoteproc/sunxi_rproc_kunit.c

[ ... ]

> +static void use_mmio_mocks(struct test_context *ctx)
> +{
> +	ctx->priv.cfg_va = (void __iomem *)ctx->mock_cfg_regs;
> +	ctx->priv.remap_va = (void __iomem *)&ctx->mock_remap_reg;
> +	ctx->priv.sram0_va = (void __iomem *)ctx->mock_sram_buf;
> +	ctx->priv.sram0_size = sizeof(ctx->mock_sram_buf);
> +	ctx->priv.sram1_va = (void __iomem *)ctx->mock_sram1_buf;
> +	ctx->priv.sram1_size = sizeof(ctx->mock_sram1_buf);
> +}

[Severity: Low]
Unlike FAKE_SRAM_VA and FAKE_SRAM1_VA, which use (void __force __iomem *),
the casts in use_mmio_mocks() omit __force when casting normal kernel
memory buffers to __iomem pointers. Should these casts use
(void __force __iomem *) to avoid Sparse address-space warnings when
building with C=1?

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20261101185735.762559-1-tcmichals@gmail.com?part=7
````

---

### 7. Uwe Kleine-König <u.kleine-koenig@baylibre.com> — Re: [PATCH v3 2/7] mailbox: sun55i: Add Allwinner A523 message box driver
- **Date**: Thu, 5 Nov 2026 14:58:45 +0100
- **Message-ID**: `<auyLOc4djmm29Bnz@monoceros>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/auyLOc4djmm29Bnz@monoceros/
- **Raw Email File**: `v3/emails/15_auyLOc4djmm29Bnz_monoceros.eml`

#### Verbatim text
````text
On Sun, Nov 01, 2026 at 12:57:30PM -0600, Tim Michals wrote:
> +#include <linux/mod_devicetable.h>
> [...]
> +#include <linux/platform_device.h>

Please don't include <linux/mod_devicetable.h> as this pulls in a lot of
unneeded build dependencies.

<linux/platform_device.h> already provides of_device_id which is all you
seem to use here. If you want full iwyu include <linux/device-id/of.h>
instead.

Best regards
Uwe
````


---

### 8. Uwe Kleine-König <u.kleine-koenig@baylibre.com> — Re: [PATCH v3 4/7] remoteproc: sunxi: Add Allwinner A523 XuanTie E906 driver
- **Date**: Thu, 5 Nov 2026 14:59:49 +0100
- **Message-ID**: `<auyLnRhPv0jz1roG@monoceros>`
- **Lore Link**: https://lore.kernel.org/linux-sunxi/auyLnRhPv0jz1roG@monoceros/
- **Raw Email File**: `v3/emails/16_auyLnRhPv0jz1roG_monoceros.eml`

#### Verbatim text
````text
Hello,

On Sun, Nov 01, 2026 at 12:57:32PM -0600, Tim Michals wrote:
> +#include <linux/mod_devicetable.h>
> [...]
> +#include <linux/platform_device.h>

As for patch 2: Please don't use <linux/mod_devicetable.h> and rely on
<linux/platform_device.h> to provide of_device_id.

Best regards
Uwe
````
