# Kernel Lore Submission Traps & Maintainer Patterns

This guide documents recurring review feedback, rejection traps, and maintainer preferences extracted from `lore.kernel.org` across `linux-remoteproc`, `linux-mailbox`, `linux-sunxi`, and `devicetree`.

---

## 1. DeviceTree Maintainer Guidelines (Krzysztof Kozlowski & Rob Herring)

### A. Binding Subjects & Naming
- **Drop "schema" and "binding" from subjects**:
  - ❌ `dt-bindings: remoteproc: add Allwinner sun55i-a523-rproc schema`
  - ✅ `dt-bindings: remoteproc: add Allwinner sun55i-a523-rproc`
- **Filename must match compatible**:
  - Filename `Documentation/devicetree/bindings/<subsystem>/<vendor>,<chip>-<device>.yaml` must match the primary `compatible: <vendor>,<chip>-<device>`.

### B. Rigid Hardware Description
- **Never use `enum:` across multiple array items**:
  Hardware register banks, interrupts, and clocks have fixed physical wiring. Random combinations or permutations are invalid. Always use positional `- const:` entries.
- **No driver prose in descriptions**:
  Do not describe how Linux software consumes the hardware (e.g. "used by the driver's workqueue"). Describe what the hardware signal or wire physically represents.
- **No `minItems` on fixed arrays**:
  If a device has 3 clocks, declare the 3 items explicitly; do not use `minItems: 1` or `minItems: 2` to create artificial flexibility.

### C. Threading & Mailing List Etiquette
- **Never thread new versions under old threads**:
  Do **NOT** use `--in-reply-to` when submitting v2, v3, etc. Every new major series must be a standalone top-level thread on the mailing lists.
- **Drop `Cc:` trailers from commit bodies**:
  Clean commit messages only. Place recipient addresses in `git send-email --to ... --cc ...`.

---

## 2. RemoteProc Maintainer Guidelines (Bjorn Andersson & Mathieu Poirier)

### A. Atomic Context in Mailbox Callbacks
- `include/linux/mailbox_client.h` documents `rx_callback` as called in atomic context (under `mbox->lock` in hardirq).
- `rproc_vq_interrupt()` leads into RPMsg receive code which takes mutexes (`rpmsg_recv_single()`).
- **Rule**: Never call `rproc_vq_interrupt()` directly in a hardirq `rx_callback`. Always queue the work to a dedicated ordered workqueue (`queue_work(priv->workqueue, &priv->vq_work)`).

### B. Carveout Registration Lifecycle
- RemoteProc core framework (`drivers/remoteproc/remoteproc_core.c`) invokes `rproc_resource_cleanup()`, which frees `rproc->carveouts` every time the core stops.
- **Rule**: Never register dynamic carveouts exclusively in `probe()`. Register carveouts in `.prepare()` so they are re-added across core restarts, crash recovery, and firmware reload cycles.

### C. Teardown Symmetry
- **Rule**: `rproc_del(rproc)` must execute **before** mailbox channels are freed (`mbox_free_channel()`).
- VirtIO device teardown generates final kicks. If mailbox channels are freed first, virtio shutdown causes a Use-After-Free (UAF) on `priv->tx_chan`.

---

## 3. Mailbox Maintainer Guidelines (Jassi Brar)

### A. Pacing: `last_tx_done` Semantics
- When a mailbox controller uses `txdone_poll = true`:
  - Returning `true` signals to the mailbox framework that the message was consumed by the remote core.
  - If `last_tx_done` returns `true` as soon as `count < FIFO_MAX`, the framework immediately dispatches the next queued message, overflowing the remote processor's FIFO.
  - **Superseded for the sun55i msgbox (owner, 2026-10-16):** a kick is a doorbell (the data is in the vrings), so `last_tx_done` returns `true` while the FIFO has room (`count < FIFO_MAX`) and the driver never writes a full FIFO. See DESIGN_DECISIONS.md "Message box TX completion".

### B. Asynchronous Payload Storage
- In polling mode, `mbox_send_message(chan, data)` takes a pointer to the data. If transmission is delayed, the framework holds that pointer and dereferences it later from a timer.
- **Rule**: Never pass stack-allocated local variables to `mbox_send_message()`. Use static immutable payloads (`static const u32 dummy = 0`) or driver private state that outlives the call.

### C. Hardirq Concurrency & TOCTOU
- Multi-channel mailbox registers must be guarded with `spin_lock_irqsave(&mbox->lock, flags)`.
- Acknowledge/clear the hardware pending bit **before** reading the FIFO entries. If a remote message arrives during the drain loop, clearing before drain ensures the pending bit is re-asserted, preventing lost doorbells.
