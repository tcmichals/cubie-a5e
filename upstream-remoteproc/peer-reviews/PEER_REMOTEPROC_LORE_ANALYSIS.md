# Upstream Lore Review & Bug History of Peer RemoteProc & Mailbox Drivers

This document analyzes public upstream review comments, maintainer feedback, and architectural bug fixes on **other accepted mainline RemoteProc and Mailbox drivers** (`stm32_rproc`, `imx_rproc`, `ti_k3`, `qcom_q6v5`, `sun6i-msgbox`) from `lore.kernel.org`.

By understanding the traps that subsystem maintainers (Mathieu Poirier, Bjorn Andersson, Jassi Brar, Rob Herring, Krzysztof Kozlowski) caught during reviews of peer drivers, we preemptively protect the Allwinner sun55i drivers from identical rejections.

---

## 1. ST STM32 (`drivers/remoteproc/stm32_rproc.c`)

### Case 1.1: Workqueue Late Execution Race After Core Halt
- **Commit**: `35bdafda40cc` ("remoteproc: stm32_rproc: Add mutex protection for workqueue")
- **Lore Thread**: [https://lore.kernel.org/r/20230331160634.3113031-1-arnaud.pouliquen@foss.st.com](https://lore.kernel.org/r/20230331160634.3113031-1-arnaud.pouliquen@foss.st.com)
- **Reviewer**: **Mathieu Poirier <mathieu.poirier@linaro.org>** (RemoteProc Subsystem Maintainer)
- **The Bug**:
  When a mailbox interrupt queues work to `vq_work`, the kworker may execute *after* `rproc_stop()` has already begun tearing down VirtIO subdevices. Calling `rproc_vq_interrupt()` while rpmsg endpoints are being destroyed causes a kernel crash:
  ```text
  virtqueue_add_inbuf
  rpmsg_recv_single
  rpmsg_recv_done
  vring_interrupt
  stm32_rproc_mb_vq_work
  process_one_work
  ```
- **Maintainer-Mandated Fix**:
  Check `rproc->state` under `rproc->lock` inside the work item:
  ```c
  mutex_lock(&rproc->lock);
  if (rproc->state != RPROC_RUNNING && rproc->state != RPROC_ATTACHED)
      goto unlock_mutex;

  rproc_vq_interrupt(rproc, mb->vq_id);
  unlock_mutex:
  mutex_unlock(&rproc->lock);
  ```
- **Direct Lesson for `sunxi_rproc.c`**:
  Our `sunxi_rproc_vq_work()` should incorporate this exact guard:
  ```c
  mutex_lock(&priv->rproc->lock);
  if (priv->rproc->state == RPROC_RUNNING) {
      rproc_vq_interrupt(priv->rproc, 0);
      rproc_vq_interrupt(priv->rproc, 1);
  }
  mutex_unlock(&priv->rproc->lock);
  ```

---

### Case 1.2: Type Safety on Resource Table Pointers
- **Commit**: `c77b35ce66af` ("remoteproc: stm32: Fix incorrect type assignment returned by stm32_rproc_get_loaded_rsc_table")
- **Commit**: `32381bbccba4` ("remoteproc: stm32: Fix incorrect type in assignment for va")
- **Lore Thread**: [https://lore.kernel.org/r/20240117135312.3381936-3-arnaud.pouliquen@foss.st.com](https://lore.kernel.org/r/20240117135312.3381936-3-arnaud.pouliquen@foss.st.com)
- **Reviewer / Tool**: Sparse static analysis & Arnaud Pouliquen
- **The Bug**:
  The resource table pointer was assigned to `void __iomem *` instead of `void *` or `struct resource_table *`, causing Sparse compiler warnings on 64-bit architectures.
- **Direct Lesson for `sunxi_rproc.c`**:
  Always use `(__force void *)` when bridging ioremapped MMIO buffers to core `struct rproc_mem_entry` virtual addresses, and keep sparse checking (`make C=1`) clean.

---

## 2. NXP i.MX (`drivers/remoteproc/imx_rproc.c`)

### Case 2.1: `NULL` vs `IS_ERR()` Return Convention Mismatch
- **Commit**: `665eebebb029` ("remoteproc: imx_rproc: Fix NULL vs IS_ERR() bug in imx_rproc_addr_init()")
- **Lore Thread**: [https://lore.kernel.org/r/20260227091546.4044246-1-nichen@iscas.ac.cn](https://lore.kernel.org/r/20260227091546.4044246-1-nichen@iscas.ac.cn)
- **The Bug**:
  `devm_ioremap()` returns `NULL` on failure, while `devm_ioremap_resource()` and `devm_ioremap_resource_wc()` return an `ERR_PTR`. In i.MX, code checking `if (!res)` on an `ERR_PTR` allowed negative pointer offsets to leak into the address table, faulting on boot.
- **Direct Lesson for `sunxi_rproc.c`**:
  Our `sunxi_rproc_register_mem()` strictly uses `IS_ERR()` for `devm_platform_ioremap_resource_byname` and `devm_ioremap_resource_wc`, and `!va` solely for `devm_ioremap`.

---

### Case 2.2: Memory Alias Boundary Off-By-One
- **Code Pattern**: `imx_rproc_da_to_va()` (lines 531-532)
- **Lore Discussion**: Boundary verification in Device Address to System Address translation.
- **The Pitfall**:
  Using `sys + len < sys_addr + size` instead of `<=`:
  ```c
  /* FLAGGED PATTERN in imx_rproc: strictly less-than */
  if (sys >= priv->mem[i].sys_addr && sys + len < priv->mem[i].sys_addr + priv->mem[i].size)
  ```
  This creates an off-by-one bug where an ELF segment touching the very last byte of the SRAM window is rejected.
- **Direct Lesson for `sunxi_rproc.c`**:
  Our driver uses `<= (priv->r_sram_phys + priv->r_sram_size)` which correctly covers the full window inclusive of the boundary.

---

## 3. TI K3 (`drivers/remoteproc/ti_k3_common.c` / `ti_k3_r5_remoteproc.c`)

### Case 3.1: Mailbox Channel Leakage on Probe Deferral
- **Commit**: `2433961962be` ("remoteproc: k3: Correctly release some resources allocated in k3_rproc_request_mbox()")
- **Lore Thread**: [https://lore.kernel.org/r/df853ede72e9972c464415990b196289680e6acb.1756065142.git.christophe.jaillet@wanadoo.fr](https://lore.kernel.org/r/df853ede72e9972c464415990b196289680e6acb.1756065142.git.christophe.jaillet@wanadoo.fr)
- **The Bug**:
  When a remoteproc driver requests multiple mailbox channels in sequence, if the second channel returns `-EPROBE_DEFER`, returning immediately without freeing the first channel leaks the client channel binding permanently in the mailbox framework.
- **Direct Lesson for `sunxi_rproc.c`**:
  With our single-channel mailbox model (D11: `mboxes = <&msgbox 9>`), channel leak on partial acquisition is physically impossible. On probe failure, `sunxi_rproc_free_mbox()` safely cleans up the channel.

---

### Case 3.2: Refactoring Reserved Memory Initialization
- **Commit**: `c52d5f1d2aac` ("remoteproc: k3: Refactor reserved_mem_init() functions into common driver")
- **Lore Thread**: [https://lore.kernel.org/r/20250513054510.3439842-36-b-padhi@ti.com](https://lore.kernel.org/r/20250513054510.3439842-36-b-padhi@ti.com)
- **Maintainer Consensus**:
  RemoteProc core framework binds `memory-region[0]` as the default DMA pool via `of_reserved_mem_device_init(dev)`. Drivers that attempt to create redundant custom DMA pools conflict with VirtIO buffer allocators.
- **Direct Lesson for `sunxi_rproc.c`**:
  Adhering to D10 (positional list with `vram` at index 0) directly follows the TI K3 pattern accepted by Mathieu Poirier and Bjorn Andersson.

---

## 4. Allwinner Legacy Mailbox (`drivers/mailbox/sun6i-msgbox.c`)

### Case 4.1: TOCTOU Race Condition on FIFO Drain
- **Upstream Discussion**: Chen-Yu Tsai / Maxime Ripard review of Allwinner Message Box.
- **The Bug**:
  In older Allwinner drivers, the interrupt handler read all FIFO messages and then cleared the pending interrupt status at the end:
  ```c
  /* BUGGY LEGACY PATTERN */
  while (read_fifo(&msg))
      process(msg);
  writel(PENDING_BIT, status_reg); /* TOCTOU: Clears message that arrived during loop! */
  ```
  If the remote coprocessor deposited a new message into the FIFO right before the `writel()`, the pending bit was cleared, leaving the message stranded in the FIFO with no subsequent interrupt.
- **Direct Lesson for `sun55i-msgbox.c`**:
  Our driver acknowledges and clears the pending bit **before** starting the drain loop:
  ```c
  writel(RD_IRQ_PEND_BIT(p), local_base + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
  for (i = 0; i < SUN55I_FIFO_MAX; i++) { ... }
  ```

---

## 5. Summary Checklist of Subsystem Maintainer Traps

| Subsystem Maintainer | Common Rejection Trap | Upstream Peer Proven Resolution |
| :--- | :--- | :--- |
| **Mathieu Poirier** | Workqueue executes after `rproc_stop()` tears down subdevices. | Lock `rproc->lock` and verify `rproc->state == RPROC_RUNNING` inside work handler. |
| **Bjorn Andersson** | Named carveouts wiped out on core reboot. | Register carveouts in `.prepare()` hook, not in `probe()`. |
| **Krzysztof Kozlowski** | `enum:` across multiple items in `reg-names` or `clock-names`. | Rigid positional items with `- const:` entries only. |
| **Krzysztof Kozlowski** | Using `--in-reply-to` on major series revisions. | Each version must be a standalone top-level thread on mailing lists. |
| **Jassi Brar** | `last_tx_done` returns `true` on buffer capacity instead of complete drain. | Must test `count == 0` (empty) to avoid VirtIO ring buffer exhaustion. **Superseded:** the project uses `count < 8` (FIFO has room) and `send_data()` refuses a full FIFO — `DESIGN_DECISIONS.md` "Message box TX completion". The `count == 0` request came from the v2 Sashiko review (UPSTREAM_LESSONS_LEARNED Lesson 2.1); this Jassi Brar attribution is unverified. |
| **Jassi Brar** | Stack-allocated pointers passed to `mbox_send_message()`. | Payloads must outlive the call (static storage or driver struct). |
