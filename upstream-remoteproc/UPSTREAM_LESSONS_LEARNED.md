# Upstream Linux Kernel Lessons Learned & Review Rules Ledger

This document is a permanent engineering ledger of anti-patterns, maintainer feedback, and hard architectural rules learned during the upstreaming of the **Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers** to `linux-remoteproc`, `linux-mailbox`, `linux-sunxi`, and `devicetree`.

Use this guide as an adversarial pre-flight checklist before submitting any future patch series to the Linux kernel mailing lists.

The owner's design decisions (what we deliberately keep even when a review disagrees) are in **[DESIGN_DECISIONS.md](DESIGN_DECISIONS.md)**.

> [!NOTE]
> For the 5-stage automated audit specification and LLM prompt templates modeling the Linux Foundation review bot, see **[scripts/sashiko_protocols.md](scripts/sashiko_protocols.md)**. The automated static audit runner is located at **[scripts/run_adversarial_audit.py](scripts/run_adversarial_audit.py)**.

---

## 1. Devicetree Schema Bindings (Krzysztof Kozlowski & Rob Herring)

### Lesson 1.1: Never Use `enum` in `reg-names` or `clock-names`
* **What Went Wrong**:
  In RFC v1 and v2, we defined:
  ```yaml
  reg-names:
    minItems: 1
    maxItems: 4
    items:
      enum:
        - cfg
        - r_sram
        - r_sram1
        - remap
  ```
  Maintainer Krzysztof Kozlowski responded:
  > *"Nope, this cannot be flexible. Please open existing code to see how this is done... So you completely ignored my feedback and sent exactly the same."*

* **The Upstream Rule**:
  DT bindings describe physical hardware. Hardware registers do not randomly change order or have arbitrary permutations. Using `items: enum: [...]` allows invalid trees like `reg-names = "remap", "cfg"` or duplicate entries.
* **The Correct Pattern**:
  Always use an ordered, positional list with `- const:`:
  ```yaml
  reg:
    items:
      - description: Configuration and boot vector registers
      - description: Dedicated MCU SRAM Space 0
      - description: Switchable MCU SRAM Space 1
      - description: Hardware SRAM remap control register

  reg-names:
    items:
      - const: cfg
      - const: r_sram
      - const: r_sram1
      - const: remap
  ```

---

### Lesson 1.2: Positional `items:` with `minItems` Instead of Free-Form Text for Interrupts
* **What Went Wrong**:
  We wrote:
  ```yaml
  interrupts:
    minItems: 1
    maxItems: 4
    description:
      One interrupt per processor port in port order (arm, dsp, cpus, rv).
      The ARM host port interrupt is required; remote port interrupts are optional.
  interrupt-names:
    minItems: 1
    maxItems: 4
    items:
      enum: [arm, dsp, cpus, rv]
  ```
  Maintainer feedback:
  > *"No, this has to be constrained/specific. See writing bindings and any existing examples. Also, just list the interrupts with minItems, instead of free form text."*

* **The Upstream Rule**:
  Do not explain ordering rules in narrative `description:` blocks when the YAML schema can enforce it programmatically.
* **The Correct Pattern**:
  Use a positional `items:` list with `minItems: 1`:
  ```yaml
  interrupts:
    minItems: 1
    items:
      - description: ARM Cortex-A55 host port interrupt
      - description: HiFi4 Audio DSP port interrupt
      - description: CPUS power management port interrupt
      - description: XuanTie RISC-V port interrupt

  interrupt-names:
    minItems: 1
    items:
      - const: arm
      - const: dsp
      - const: cpus
      - const: rv
  ```
  This guarantees that position 0 is always `arm` (mandatory), and any optional interrupts (`dsp`, `cpus`, `rv`) must appear in that exact hardware sequence.

---

### Lesson 1.3: Do NOT Thread New Revisions Under Older Versions (`In-Reply-To`)
* **What Went Wrong**:
  We sent v2 as an `In-Reply-To` response to the v1 cover letter.
  Maintainer feedback:
  > *"Do not attach (thread) your patchsets to some other threads (unrelated or older versions). This buries them deep in the mailbox and might interfere with applying entire sets. See Documentation/process/submitting-patches.rst#L830"*

* **The Upstream Rule**:
  Each patch version (`[PATCH v1]`, `[PATCH v2]`, `[PATCH v3]`) must be sent as a **clean, independent top-level thread**.
  * Chaining revisions buries patches in existing email threads.
  * Automated patchwork and maintainer tools (`b4 am`, `git am`) fail to parse entire series cleanly when threaded as replies.
  * Instead, reference prior revisions in the cover letter body using their Lore URL / message-id.

---

### Lesson 1.4: One Silicon Die = Single Compatible String
* **What Went Wrong**:
  In v1, we defined `compatible: enum: [allwinner,sun55i-a523-rproc, allwinner,sun55i-a527-rproc, allwinner,sun55i-t527-rproc]`.
* **The Upstream Rule**:
  If multiple marketing chip names (A523, A527, T527) share the exact same silicon die, upstream DT policy mandates using a **single compatible string** based on the earliest introduced part (`allwinner,sun55i-a523-rproc`). Do not inflate DT schemas with identical hardware compatibles.

---

### Lesson 1.5: List the `memory-region` Items; Do Not Rely on Node Names
* **What Went Wrong**:
  In v1 the rproc binding described `memory-region` / `memory-region-names` in free text. Krzysztof asked us to list the items (v1, KK8). The vendor BSP, and older stm32 / i.MX / ZynqMP bindings, use a free list whose meaning comes from reserved-memory node names (`vdev0buffer`, `vdev0vring0`, ...), which a new binding should not copy.
* **The Upstream Rule**:
  A new binding lists each `memory-region` entry positionally, with matching `- const:` names. The schema can check positions and names; it cannot check node names.
* **The Pattern We Use** (TI K3, MediaTek SCP, OMAP do the same):
  ```yaml
  memory-region:
    minItems: 1
    items:
      - description: Virtio vring and buffer pool
      - description: Firmware code and data in DRAM
      - description: Trace buffer
  memory-region-names:
    minItems: 1
    items:
      - const: vram
      - const: dram
      - const: trace
  ```
  Index 0 is a `shared-dma-pool`; the remoteproc core binds it to the virtio device when no `vdev0buffer` carveout exists. Decision record: [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) (D10).

---

## 2. Mailbox Subsystem Rules (Jassi Brar & Sashiko AI)

### Lesson 2.1: `last_tx_done()` Policy Is a Decision — v3 Uses "Remote Drained" (`count == 0`)
> **Status (2026-10-13):** This is a design choice, not a framework rule. The v3 driver follows
> Sashiko's v2 comment and upstream `sun6i-msgbox` (`count == 0`, poll). The Allwinner vendor BSP
> (`drivers/msgbox/sunxi-msgbox.c`) instead treats "FIFO not full" as done with a 5 ms poll, or uses
> the msgbox write IRQ (`txdone_irq`). The drain policy costs throughput: back-to-back kicks wait for a
> poll tick. Revisit after v3 with a benchmark; do not flip it because of a review comment alone.

* **What Went Wrong**:
  ```c
  /* WRONG: Checking available FIFO headroom */
  static bool sun55i_msgbox_last_tx_done(struct mbox_chan *chan) {
      count = readl(status_reg) & MSG_NUM_MASK;
      return count < SUN55I_FIFO_MAX;
  }
  ```
* **The Upstream Rule**:
  In Linux Mailbox client-controller contracts:
  * Returning `true` signals to the client that the previously queued message has finished its lifecycle.
  * If `last_tx_done()` returns `true` simply because there are 7 messages in an 8-entry FIFO, the client immediately floods another message, overrunning the remote processor before it can service the interrupt.
* **The Correct Pattern**:
  ```c
  /* CORRECT: Returning true only when remote processor completely emptied the FIFO */
  static bool sun55i_msgbox_last_tx_done(struct mbox_chan *chan) {
      count = readl(status_reg) & MSG_NUM_MASK;
      return count == 0;
  }
  ```

---

### Lesson 2.2: Hardirq MMIO FIFO Drain Requires SMP Spinlock Protection
* **What Went Wrong**:
  In `sun55i_msgbox_irq()`, reading status, clearing pending bits, and reading the message FIFO was executed without a spinlock.
* **The Upstream Rule**:
  On multi-core SMP systems, a shared level interrupt or shared line can fire across multiple CPUs simultaneously. Without locking, CPU 0 and CPU 1 can both read `MSG_STATUS > 0` and attempt to pop `MSG_FIFO`, causing hardware FIFO underflow and data corruption (TOCTOU race).
* **The Correct Pattern**:
  Enclose status checks, acknowledgments, and FIFO drains in `spin_lock_irqsave(&mbox->lock, flags)`.

---

### Lesson 2.3: Teardown Order: Mask and `synchronize_irq()` BEFORE Unregistering
* **What Went Wrong**:
  Calling `mbox_controller_unregister()` first, then disabling clocks and asserting reset.
* **The Upstream Rule**:
  If an interrupt fires on another CPU while `mbox_controller_unregister()` is executing, the ISR runs after `chan->cl` is set to NULL, triggering a NULL pointer dereference.
* **The Correct Pattern**:
  1. Mask all interrupt enable bits in MMIO.
  2. Call `synchronize_irq(irq)` on all registered lines so in-flight handlers finish.
  3. Call `mbox_controller_unregister()`.
  4. Free IRQs with `free_irq()`.
  5. Assert reset and disable clocks.

---

## 3. RemoteProc Subsystem Rules (Bjorn Andersson & Mathieu Poirier)

### Lesson 3.1: RemoteProc Teardown Order: `rproc_del()` MUST Be Called First
* **What Went Wrong**:
  In early revisions, an inverted order freed mailbox channels before calling `rproc_del()`:
  ```c
  /* WRONG: Frees mailbox channels while remote core and virtio devices are still active */
  mbox_free_channel(priv->rx_chan);
  mbox_free_channel(priv->tx_chan);
  cancel_work_sync(&priv->vq_work);
  rproc_del(rproc);
  ```
* **The Upstream Rule**:
  `rproc_del()` shuts down the remoteproc framework (`rproc_shutdown()`), stops the remote core via `ops->stop()`, and unregisters VirtIO devices. During VirtIO teardown, drivers flush buffers and may kick the mailbox. If mailbox channels are freed *before* `rproc_del()`, VirtIO teardown will attempt to access freed or NULL mailbox channels.
* **The Correct Pattern**:
  In `remove()`:
  1. `rproc_del(rproc)` — Halt remote core and unregister VirtIO devices first.
  2. `sunxi_rproc_free_mbox(priv)` — Free mailbox channels after `rproc_del()` returns, preventing kick after free.
  3. `cancel_work_sync(&priv->vq_work)` — Drain any remaining worker items.
  4. `of_reserved_mem_device_release()` — Release reserved memory last.
  (The crash IRQ is devm-managed and disarmed in `stop()`).

---

### Lesson 3.1b: Never Call `disable_irq()` Under a Spinlock (Sleep in Atomic Context)
* **What Went Wrong**:
  Wrapping `disable_irq()` inside `spin_lock_irqsave(&priv->lock, flags)` or holding a lock to guard an enable flag.
* **The Upstream Rule**:
  `disable_irq()` calls `synchronize_irq()`, which waits for in-flight interrupt handlers and **sleeps** (`kernel/irq/manage.c`). Calling it while holding a spinlock triggers `might_sleep()` and kernel panics (`BUG: scheduling while atomic`). Furthermore, rproc ops are already serialized by `rproc->lock`.
* **The Correct Pattern**:
  Use atomic bit operations (`test_and_clear_bit` / `test_and_set_bit`) without a spinlock:
  ```c
  /* In crash handler (interrupt context) */
  if (test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
      disable_irq_nosync(irq);

  /* In stop() (process context) */
  if (priv->crash_irq > 0 &&
      test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
      disable_irq(priv->crash_irq);
  ```

---

### Lesson 3.1c: Static Carveouts Must Be Registered in `.prepare()`
* **What Went Wrong**:
  Registering static carveouts in `probe()` via `rproc_add_carveout()`.
* **The Upstream Rule**:
  `rproc_shutdown()` invokes `rproc_resource_cleanup()`, which purges and frees all entries in `rproc->carveouts`. If carveouts are only registered during `probe()`, they vanish after the first stop, breaking subsequent boots.
* **The Correct Pattern**:
  Register static carveouts inside `rproc->ops->prepare()`, matching upstream `imx_rproc` and `stm32_rproc`. `prepare()` is invoked on every boot, ensuring carveouts exist on every lifecycle run.

---

### Lesson 3.1d: `devm_memremap()` Returns `ERR_PTR`, Never `NULL`
* **What Went Wrong**:
  Checking the return value of `devm_memremap()` with `if (!va)` or `if (va)`.
* **The Upstream Rule**:
  `devm_memremap()` returns an `ERR_PTR` on failure (e.g. `-ENOMEM` or `-EINVAL`), never `NULL`. Testing `if (va)` treats error pointers as valid mappings, passing corrupt pointers to `rproc_add_carveout()`.
* **The Correct Pattern**:
  Always check `IS_ERR(va)` and propagate `PTR_ERR(va)`.

---

### Lesson 3.1e: Do Not Match on `memory-region-names` in the Driver
* **What Went Wrong**:
  Using `strstr(name, "dram") || strstr(name, "vram")` caused both DT regions to match the same branch, overwriting `priv->dram_va` with whichever region came last. The v3 fix switched to `strcmp()`, but the driver still kept its own DRAM/trace lookup that duplicated the core (F7).
* **The Upstream Rule**:
  Register every region as a carveout named after its `memory-region-names` entry and let the remoteproc core resolve addresses from the carveout list. The driver uses the names only as labels; behaviour follows from the position (index 0 = DMA pool). Since D7 the driver has no name comparisons at all.

---

### Lesson 3.2: RemoteProc Doorbell Protocol: `cl.knows_txdone = false`, Controller Paces TX
> **Superseded (2026-10-13).** An earlier version of this lesson said to set `knows_txdone = true` and
> call `mbox_client_txdone()` after every send. That contradicts the controller's own pacing and was
> flagged by Sashiko on v2 ("immediate txdone corrupts mailbox"). The code and this ledger now agree.
* **What Went Wrong**:
  The kick protocol was flipped between versions: polling (`knows_txdone = false`, 1 ms poll latency per queued kick) → client-reported completion (`knows_txdone = true` + `mbox_client_txdone()`) → back to polling. Each flip was driven by a review comment, not a decision.
* **The Rule**:
  1. The sun55i msgbox controller sets `txdone_poll = true`, so the controller decides when a message is done (`last_tx_done()`, see Lesson 2.1).
  2. The client sets `cl.knows_txdone = false` and `cl.tx_block = false`, and never calls `mbox_client_txdone()`.
  3. Payloads must outlive `mbox_send_message()` because the core queues the pointer: use immutable `static const u32` values, one per vring (no shared writable `kick_msg`).
  4. The Allwinner vendor BSP uses the same client settings (`knows_txdone` unset, `tx_block = false`).
* **Known cost and the real fix**: kicks queued behind an unconsumed kick wait for the poll tick, and more than `MBOX_TX_QUEUE_LEN` (20) queued kicks return `-ENOBUFS`. The fix is in the controller (write-IRQ `txdone_irq`, or a different `last_tx_done` policy), measured on the board — not `knows_txdone = true` in the client.

---

### Lesson 3.2b: Mailbox `rx_callback` Is Atomic — Defer rpmsg Work
* **What Went Wrong**:
  The rx callback called `rproc_vq_interrupt()` directly (commit `45501e5ae8a4`). The sun55i msgbox calls `rx_callback` from its hardirq handler with `mbox->lock` held, and the virtio/rpmsg receive path takes mutexes (`rpmsg_recv_single()`). This is a sleep in atomic context on every received message on a non-RT kernel. The board runs PREEMPT_RT, which force-threads IRQ handlers, so silicon tests never showed it.
* **The Rule**:
  `include/linux/mailbox_client.h` documents `rx_callback` as an atomic callback. Queue a work item and call `rproc_vq_interrupt()` from it (vendor BSP and `stm32_rproc` do the same). Free the mailbox channels before `cancel_work_sync()`.
* **How to catch it**: run once per version on a non-RT kernel with `CONFIG_DEBUG_ATOMIC_SLEEP=y` and `CONFIG_PROVE_LOCKING=y`.

---

### Lesson 3.3: ARM64 MMU Attribute Conflicts: Normal DDR vs `ioremap_wc`
* **What Went Wrong**:
  In `sunxi_rproc_parse_memory_regions()`, all carveouts were marked `mem->is_iomem = true`.
* **The Upstream Rule**:
  On ARM64, the same physical range must not be mapped twice with different memory attributes (WB vs WC vs Device). The project's DRAM regions are `reserved-memory` with `no-map`, so the kernel linear map does **not** cover them; the DMA pool (`shared-dma-pool`) maps its region write-combining.
  `is_iomem` tells the remoteproc core which copy routine to use (`memcpy_toio` vs `memcpy`); it is not a cache attribute.
* **The Correct Pattern**:
  * On-chip SRAM (`r_sram`, `r_sram1`): `devm_ioremap_wc()`, `mem->is_iomem = true`.
  * DRAM `no-map` regions (`trace`, `dram`, vrings): map once with the same attribute as the DMA pool (`devm_memremap(..., MEMREMAP_WC)`), `mem->is_iomem = false`.
  * (An earlier version of this lesson said DRAM is "already mapped WB"; that is only true for regions without `no-map`, which this project does not use.)

---

### Lesson 3.4: Boot Vector Interconnect Write-Back Flush
* **What Went Wrong**:
  Writing `STA_ADD_REG` with `writel()` and immediately deasserting `rst_core`.
* **The Upstream Rule**:
  On modern AXI/interconnect buses, CPU writes are posted and buffered. If `rst_core` is released before the posted write reaches the physical hardware register, the core may fetch its reset vector from the old or uninitialized boot register value.
* **The Correct Pattern**:
  Always follow critical configuration writes with a dummy read-back before asserting or releasing resets:
  ```c
  writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);
  (void)readl(priv->cfg_va + cfg->boot_reg_offset); /* Posted write flush */
  reset_control_deassert(priv->rst_core);
  ```

---

### Lesson 3.5: Schema-to-C-Code 1:1 Parity (No Dead Resource Queries)
* **What Went Wrong**:
  When Devicetree binding maintainers required moving `dram` and `trace` from `reg` into `memory-region`, `sunxi_rproc_parse_memory_regions()` was implemented, but old `platform_get_resource_byname(pdev, IORESOURCE_MEM, "dram")` and `"trace"` calls in `sunxi_rproc_register_mem()` were left behind. Because `platform_get_resource_byname()` quietly returns `NULL`, the dead code remained invisible.
* **The Upstream Rule**:
  Every resource queried by `platform_get_resource_byname(..., IORESOURCE_MEM, "name")` in `register_mem()` MUST have a corresponding entry in `reg-names` in the YAML binding schema. Unmatched queries indicate dead code or schema divergence.
* **The Correct Pattern**:
  `register_mem()` queries *only* `cfg`, `r_sram`, `r_sram1`, `remap`. `parse_memory_regions()` handles all `memory-region` carveouts.

---

### Lesson 3.6: State Machine Completion vs Syntactic Error Checks
> **Note (2026-10-13):** The example is the old `knows_txdone = true` client. The project now uses `knows_txdone = false` (Lesson 3.2), where the controller completes TX and the client must *not* call `mbox_client_txdone()`. The general lesson — check the success path too — still applies.
* **What Went Wrong**:
  Review linters and bots verified `if (ret < 0) dev_err(...)` on `mbox_send_message()` and considered the code complete. They completely missed that in `cl.knows_txdone = true` mode, the **pass branch (`ret >= 0`)** requires `mbox_client_txdone()` to clear `chan->active_req`.
* **The Upstream Rule**:
  Review checks must evaluate Linux subsystem state transitions on both error AND success paths, not just look for negative return checks.
* **The Correct Pattern**:
  Always verify the entire subsystem transaction lifecycle: transmit $\rightarrow$ pass path ACK $\rightarrow$ error path unwind.

---

## 4. In-Tree KUnit Testing Rules

### Lesson 4.1: Mock MMIO Must Be Endian-Safe
* **What Went Wrong**:
  In KUnit test mocks, registers were verified by directly indexing raw mock arrays:
  ```c
  /* FAILS on Big-Endian architectures */
  KUNIT_EXPECT_EQ(test, ctx->mock_cfg_regs[E906_STA_ADD_REG / 4], 0x40014000U);
  ```
* **The Upstream Rule**:
  `writel()` performs a `cpu_to_le32()` byte-swap. On Big-Endian platforms (e.g. s390, armeb), reading the mock array directly without swapping causes unit test failures.
* **The Correct Pattern**:
  Always access mock MMIO through proper kernel accessors:
  ```c
  /* Endian-safe across all CPU architectures */
  KUNIT_EXPECT_EQ(test, readl(ctx->priv.cfg_va + E906_STA_ADD_REG), 0x40014000U);
  ```

---

### Lesson 4.2: Every Test Must Be Able to Fail
* **What Went Wrong**:
  v1 tests compared register macros with the same literals (Sashiko v1). By v3 the suites still had padding that a maintainer would read as generated: tests with no assertion (`kick` with no channel), tests that only checked ops pointers were non-NULL, tests that read back values the fixture had just set, an IRQ test that "verified" a write-1-to-clear by reading a value the test itself had written, and a stale-FIFO test that passed only because of the `FIFO_MAX` cap (Sashiko v2). The test patches were also larger than the drivers.
* **The Rule**:
  1. A test must fail if the driver behaviour it names is broken. If it cannot, drop it.
  2. Do not assert a register value the test wrote itself before the call; start from a different value.
  3. One test per behaviour; no near-duplicates for coverage counts. Neither remoteproc nor mailbox has driver KUnit tests upstream yet, so the bar is high.
  4. Fake MMIO (plain memory behind `readl()`/`writel()`) only behaves like registers on some architectures: `depends on ARM64 || COMPILE_TEST`.
  5. Put the KUnit patches last in the series and say in the cover letter that they can be dropped without affecting the drivers.

---

## 5. Automated Multi-Stage Adversarial Review Protocols

To prevent regressions against these kernel rules, review checks are codified into formal protocol stages:
* 📜 **[scripts/sashiko_protocols.md](scripts/sashiko_protocols.md)**: Full prompt specification and invariant definitions for the 5 review stages:
  1. *Stage 1 (Hardirq & Concurrency)*: SMP spinlocks, TOCTOU windows, bounded loops.
  2. *Stage 2 (Resource Lifecycle & Teardown)*: LIFO reverse unwinds, workqueue teardown, UAF prevention.
  3. *Stage 3 (Subsystem Framework Contracts)*: Mailbox `last_tx_done` pacing (v3 decision: `count == 0`), client `knows_txdone = false`, `rx_callback` atomic context, RemoteProc ATT bounds.
  4. *Stage 4 (Interconnect, MMIO & Endianness)*: Posted write flushes, Big-Endian mock accessors.
  5. *Stage 5 (Adversarial Gatekeeper)*: False-positive elimination and severity scoring.
* 🛠️ **[scripts/run_adversarial_audit.py](scripts/run_adversarial_audit.py)**: Automated static audit tool running these checks before every submission and outputting results directly into `v<N>/AUDIT.md`.

---

## Summary Checklist Before Next Patch Submission

- [ ] Every `reg-names`, `clock-names`, `reset-names` uses positional `- const:` lists (NO `enum`).
- [ ] Every `interrupts` and `interrupt-names` uses positional `items:` with `minItems`.
- [ ] `make dt_binding_check` passes with **0 errors, 0 warnings**.
- [ ] `scripts/checkpatch.pl --strict` passes with **0 errors, 0 warnings**.
- [ ] Mailbox `last_tx_done()` checks `count == 0` (remote drain confirmation, mirroring sibling `sun6i-msgbox`).
- [ ] RemoteProc kick uses `cl.knows_txdone = false`, `tx_block = false`, immutable per-vring payloads; never calls `mbox_client_txdone()`.
- [ ] Mailbox `rx_callback` only queues work; `rproc_vq_interrupt()` runs from the work item.
- [ ] Teardown: `rproc_del()` → `mbox_free_channel()` → `cancel_work_sync()` (probe error path: free mailbox → cancel work).
- [ ] One non-RT kernel run with `CONFIG_DEBUG_ATOMIC_SLEEP=y` + `CONFIG_PROVE_LOCKING=y` (PREEMPT_RT hides atomic-context bugs).
- [ ] Every hardware fact in commit messages, cover letter and bindings checked against silicon readbacks or the vendor BSP (`old-kernels/linux-aw2501/.git/modules/bsp`), not against project docs alone.
- [ ] No sleeping calls (disable_irq(), synchronize_irq()) invoked under spinlocks.
- [ ] Static memory carveouts registered in .prepare() so they persist across core reboots.
- [ ] Mailbox driver only requests the ARM host port interrupt that Linux services.
- [ ] `memory-region` / `memory-region-names` listed positionally (Lesson 1.5); no driver logic keyed on region names.
- [ ] Every KUnit test can fail; KUnit patches last and marked droppable in the cover letter (Lesson 4.2).
- [ ] Nothing in the series contradicts [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) without the owner's sign-off.
- [ ] `git send-email` dispatched as an independent top-level thread (NO `--in-reply-to` linking to previous versions).
