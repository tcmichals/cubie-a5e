# Upstream Linux Kernel Lessons Learned & Review Rules Ledger

This document is a permanent engineering ledger of anti-patterns, maintainer feedback, and hard architectural rules learned during the upstreaming of the **Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers** to `linux-remoteproc`, `linux-mailbox`, `linux-sunxi`, and `devicetree`.

Use this guide as an adversarial pre-flight checklist before submitting any future patch series to the Linux kernel mailing lists.

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

## 2. Mailbox Subsystem Rules (Jassi Brar & Sashiko AI)

### Lesson 2.1: `last_tx_done()` Means "Remote Consumed", NOT "FIFO Has Free Space"
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

### Lesson 3.1e: Exact Match on `memory-region-names`
* **What Went Wrong**:
  Using `strstr(name, "dram") || strstr(name, "vram")` caused both DT regions to match the same branch, overwriting `priv->dram_va` with whichever region came last.
* **The Upstream Rule**:
  Use exact string matching (`!strcmp(rmem->name, "dram")`) for distinct memory region declarations.

---

### Lesson 3.2: RemoteProc Doorbell Protocol: `cl.knows_txdone = true` with Mandatory Pass-Case `mbox_client_txdone()`
* **What Went Wrong**:
  In early revisions, RemoteProc kicks either relied on software `hrtimer` polling (`knows_txdone = false`), adding 1 ms latency per kick, OR set `knows_txdone = true` without calling `mbox_client_txdone()` on successful sends.
* **The Upstream Rule**:
  In the Linux Mailbox framework (`drivers/mailbox/mailbox.c`), RemoteProc virtqueue kicks are fire-and-forget doorbells.
  1. Setting `cl.knows_txdone = true` bypasses software timer polling and eliminates 1 ms latency.
  2. **Mandatory Pass-Case Handling**: When `cl.knows_txdone = true`, `mbox_send_message()` marks `chan->active_req`. The client MUST handle the pass case (`ret >= 0`) by calling `mbox_client_txdone(chan, 0)` immediately:
     ```c
     ret = mbox_send_message(priv->tx_chan, &msg);
     if (ret < 0)
         dev_err_ratelimited(priv->dev, "failed to send mailbox kick: %d\n", ret);
     else
         mbox_client_txdone(priv->tx_chan, 0);
     ```
  3. Omitting `mbox_client_txdone()` in the pass case leaks `chan->active_req`, queuing subsequent kicks into the 20-message software FIFO until it overflows with `-ENOBUFS` and floods dmesg with `Try increasing MBOX_TX_QUEUE_LEN`.

---

### Lesson 3.3: ARM64 MMU Attribute Conflicts: Normal DDR vs `ioremap_wc`
* **What Went Wrong**:
  In `sunxi_rproc_parse_memory_regions()`, all carveouts were marked `mem->is_iomem = true`.
* **The Upstream Rule**:
  On ARM64, physical memory allocated via standard CMA / reserved-memory (like DDR trace buffers or virtio buffers) is already mapped by the kernel page tables as **Write-Back (WB) Cacheable**.
  If remoteproc maps it with `ioremap_wc()` (Device / Non-cacheable) while claiming `is_iomem = true`, the CPU architecture encounters **conflicting memory attributes** for the same physical address, which causes unpredictable behavior or hardware bus aborts.
* **The Correct Pattern**:
  * On-chip SRAM (`r_sram`, `r_sram1`): `mem->is_iomem = true` (I/O memory).
  * System DRAM carveouts (`trace`, `dram`, VirtIO vrings): `mem->is_iomem = false` (Normal memory).

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

## 5. Automated Multi-Stage Adversarial Review Protocols

To prevent regressions against these kernel rules, review checks are codified into formal protocol stages:
* 📜 **[scripts/sashiko_protocols.md](scripts/sashiko_protocols.md)**: Full prompt specification and invariant definitions for the 5 review stages:
  1. *Stage 1 (Hardirq & Concurrency)*: SMP spinlocks, TOCTOU windows, bounded loops.
  2. *Stage 2 (Resource Lifecycle & Teardown)*: LIFO reverse unwinds, workqueue teardown, UAF prevention.
  3. *Stage 3 (Subsystem Framework Contracts)*: Mailbox `last_tx_done` pacing (`count < FIFO_MAX`), RemoteProc ATT bounds, and `mbox_client_txdone()` pass-case completion.
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
- [ ] RemoteProc kick sets `cl.knows_txdone = true` AND calls `mbox_client_txdone()` on `ret >= 0` pass case.
- [ ] Teardown sequence halts remoteproc first (rproc_del() called before freeing mailbox channels).
- [ ] No sleeping calls (disable_irq(), synchronize_irq()) invoked under spinlocks.
- [ ] Static memory carveouts registered in .prepare() so they persist across core reboots.
- [ ] Mailbox driver only requests the ARM host port interrupt that Linux services.
- [ ] `git send-email` dispatched as an independent top-level thread (NO `--in-reply-to` linking to previous versions).
