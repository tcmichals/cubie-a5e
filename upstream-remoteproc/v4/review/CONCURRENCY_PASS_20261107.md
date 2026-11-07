# v4 Concurrency Pass Through the Core (2026-11-07)

PRESEND_CHECKLIST §3, first item. Code: `cubie-linux-7.1` at `77398ba801c3` (`linux-v4-71`),
core files from the same tree (7.1): `drivers/mailbox/mailbox.c`,
`drivers/remoteproc/remoteproc_core.c`, `drivers/rpmsg/virtio_rpmsg_bus.c`.

Core facts used below:

- `msg_submit()` calls `send_data()` under `chan->lock` (irqsave), only when
  `active_req == MBOX_NO_MSG`; a nonzero return leaves the message queued.
- `tx_tick()` swaps `active_req` to `MBOX_NO_MSG` under `chan->lock`, then calls `msg_submit()`,
  then `tx_done()`.
- `mbox_send_message()` with `tx_block` calls `tx_tick(chan, -ETIME)` on timeout.
- `mbox_free_channel()` calls `shutdown()` first, then clears `cl` and `active_req` under
  `chan->lock`.

## (a) A blocking send that times out

| Step | Result |
|---|---|
| M1 held (FIFO full), the timeout fires, nothing else is queued | `tx_tick(-ETIME)`: `active_req` cleared. M1 stays held; the armed write IRQ later writes it (a late doorbell; the data is a copied `u32`, no pointer kept) and calls `mbox_chan_txdone()`. `tx_tick()` sees `MBOX_NO_MSG` and returns. **OK** |
| M1 held, the timeout fires, M2 queued | `msg_submit()` → `send_data(M2)` clears `tx_held` under `mbox->lock` before holding or writing M2. M1 dropped, as the comment says. **OK** (`test_held_message_dropped_after_timeout`) |
| The write IRQ for M1 has dropped `mbox->lock` but not yet called `mbox_chan_txdone()`; the timeout on another CPU submits M2, which is held (FIFO full again) | The handler's `tx_tick()` completes M2 while it is still held. If M3 is queued, `send_data(M3)` clears `tx_held` and **M2 is never written**. |

**Finding C1 (Low).** The last row needs a blocking client whose timeout expires in the same few
microseconds as the write IRQ, with the FIFO full again. A late `mbox_chan_txdone()` after a
timeout completes the wrong request in every `txdone_irq` controller; that part is the core. In
this driver the consequence is a dropped doorbell, not an early completion. The only client,
`sunxi_rproc`, uses `tx_block = false`, so it never reaches this path. A lost doorbell only
delays vring processing until the next kick. No change proposed; owner to decide whether the
cover letter or a comment says so.

## (b) Unbind during traffic

`mbox_free_channel()` → `shutdown()`:

1. Under `mbox->lock`: set `shutting_down`, clear the read enable, drop `tx_held`, disarm the
   write IRQ. From here `send_data()` returns `-ESHUTDOWN`, and the handlers skip the channel
   (the enable bits are clear).
2. `synchronize_irq(mbox->irq)`: an RX handler that already drained is done delivering.
3. `tx_irq_put()`: `free_irq()` for the last user, otherwise `synchronize_irq()` on the line.
   A TX handler that saw this channel before step 1 has finished its `mbox_chan_txdone()`. That
   `tx_tick()` may call `send_data()`, which returns `-ESHUTDOWN`; the message stays queued and
   `mbox_free_channel()` drops it.
4. Drain what arrived meanwhile, under the lock.

`chan->cl` is still set during all of `shutdown()`, so a callback from step 2 or 3 is valid.
**OK.** Remoteproc remove order: `rproc_del()` → `mbox_free_channel()` → `cancel_work_sync()`.
After the free, no rx callback can queue the work, so the cancel is final. **OK**

## (c) A second interrupt after `synchronize_irq()`

- RX (SPI 0, shared by all channels): after step 1 the channel's read enable is clear, so a new
  interrupt for another channel never touches this one (`en & pend`). **OK**
- TX line still used by another channel on the same route (`users > 1`): the line stays
  requested and can fire again; this channel's write enable is clear, and `send_data()` cannot
  set it (`shutting_down`). **OK** (`test_tx_irq_send_during_shutdown_refused`,
  `test_tx_irq_shutdown_disarms_after_handler`)
- Level write IRQ: the handler clears the enable before acking, so it cannot storm. **OK**
- PREEMPT_RT: both handlers are force-threaded; `synchronize_irq()` and `free_irq()` wait for
  the thread. `mbox->lock` is a spinlock (sleeping on RT), taken only in task or threaded-IRQ
  context there. **OK**

## (d) Stop or recovery while the work runs

`sunxi_rproc_vq_work()` takes `rproc->lock` and walks `notifyids` only in `RPROC_RUNNING`.

- `rproc_shutdown()` holds `rproc->lock` across `rproc_stop()` (subdevices removed,
  `ops->stop()`, state `OFFLINE`) and `rproc_resource_cleanup()`; `rproc_trigger_recovery()`
  holds it across `rproc_stop()` and `rproc_start()` (no cleanup in between). A work
  item that waits for the lock then sees `OFFLINE` and returns; after recovery it sees
  `RUNNING` and walks the new, valid `notifyids`. **OK**
- Nothing waits for the work while holding `rproc->lock`: `cancel_work_sync()` is only in
  remove, after `rproc_del()`. `stop()` does not cancel it, which would deadlock (V4-VQWORK).
  **OK**
- Virtio teardown inside `rproc_stop()` may kick: the mailbox is still open. **OK**

**Finding C2 (Low, as imx_rproc).** One mailbox channel carries every vring, so one work item
handles both the RX and the TX vring. Suppose an rpmsg rx callback (running in the work) calls
`rpmsg_send()` when all TX buffers are in use. `virtio_rpmsg_bus` then sleeps up to 15 s in
`wait_event_interruptible_timeout()` for `rpmsg_xmit_done()`, which only this same work would
call. It wakes on the timeout and reclaims the buffers itself (`get_a_tx_buf()` calls
`virtqueue_get_buf()`), so this is a stall, not a deadlock. While it sleeps it holds
`rproc->lock`, so a stop waits too. `imx_rproc_vq_work()` has the same single work, without the
lock. `samples/rpmsg/rpmsg_client_sample.c` sends from its callback. Our firmware tests do not
exhaust TX buffers from a callback. No change proposed.

## Result

No High or Medium findings. C1 and C2 are Low; both are for the owner (yes/no on mentioning them).
