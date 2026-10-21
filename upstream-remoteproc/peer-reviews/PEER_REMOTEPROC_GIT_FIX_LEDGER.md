# Mainline Linux RemoteProc & Mailbox Peer Git Fix & Lore Ledger

This document is an exhaustive engineering ledger analyzing upstream bug fixes (`Fixes:` commits), regression histories, and maintainer reviews on **other accepted mainline RemoteProc and Mailbox drivers** in `drivers/remoteproc/` and `drivers/mailbox/`.

It extracts the exact failure modes, reviewer discussions from `lore.kernel.org`, and maintainer mandates from **Mathieu Poirier**, **Bjorn Andersson**, and **Jassi Brar**, showing how the **Allwinner sun55i (A523/A527/T527) RemoteProc and Message Box drivers** are engineered to prevent these exact failure modes.

---

## 1. RemoteProc Subsystem Bug Fixes (`drivers/remoteproc/`)

### Case 1: Workqueue Late Execution Race After Core Halt (`stm32_rproc.c`)
- **Commit**: `35bdafda40cc` ("remoteproc: stm32_rproc: Add mutex protection for workqueue")
- **Follow-up Fix**: `c3281abea67c` ("remoteproc: stm32_rproc: Fix mailbox interrupts queuing")
- **Lore Link**: [https://lore.kernel.org/r/20230331160634.3113031-1-arnaud.pouliquen@foss.st.com](https://lore.kernel.org/r/20230331160634.3113031-1-arnaud.pouliquen@foss.st.com)
- **Maintainer Suggested-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **The Bug**:
  When a mailbox interrupt defers work to a kworker, the workqueue may execute late *after* `rproc_stop()` has already begun tearing down VirtIO subdevices. Calling `rproc_vq_interrupt()` while rpmsg endpoints are being destroyed causes a kernel panic:
  ```text
  virtqueue_add_inbuf
  rpmsg_recv_single
  rpmsg_recv_done
  vring_interrupt
  stm32_rproc_mb_vq_work
  process_one_work
  ```
- **Maintainer-Mandated Solution**:
  Acquire `rproc->lock` and verify that `rproc->state` is `RPROC_RUNNING` or `RPROC_ATTACHED` before invoking `rproc_vq_interrupt()`.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  In `sunxi_rproc_vq_work()`, check `priv->rproc->state == RPROC_RUNNING` under `priv->rproc->lock` so that any late-scheduled work item dropped after core stop exits cleanly without touching torn-down vrings.

---

### Case 2: Multi-Start/Stop `-EBUSY` Memory Reservation Failure (`imx_dsp_rproc.c`)
- **Commit**: `a84a1e21c067` ("remoteproc: imx_dsp_rproc: Fix multiple start/stop operations")
- **Lore Link**: [https://lore.kernel.org/r/20251210154906.99210-1-daniel.baluta@nxp.com](https://lore.kernel.org/r/20251210154906.99210-1-daniel.baluta@nxp.com)
- **Maintainer Signed-off-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **The Bug**:
  Calling `echo start > .../state` $\rightarrow$ `echo stop > .../state` $\rightarrow$ `echo start > .../state` failed on the second start with:
  `-sh: echo: write error: Device or resource busy`
  Root cause: `devm_ioremap_resource_wc()` was called inside `prepare()` / `add_carveout()`, which reserved the physical memory region (`request_mem_region`). On the second start, the driver attempted to re-reserve an already-claimed resource.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  All memory mapping is performed **once** in `probe()` (`sunxi_rproc_register_mem()` and `sunxi_rproc_parse_memory_regions()`). In `.prepare()`, only the lightweight `rproc_add_carveout()` is called using pre-mapped pointers, guaranteeing that repeated `start`/`stop` cycles never fail with `-EBUSY`.

---

### Case 3: SRAM Phandle Offset Parsing in Multi-Bank DT (`xlnx_r5_remoteproc.c`)
- **Commit**: `d116bccf6f1c` ("remoteproc: xlnx: Fix sram property parsing")
- **Author**: **Tim Michals <tcmichals@yahoo.com>**
- **Maintainer Signed-off-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **Lore Link**: [https://lore.kernel.org/r/20260204202730.3729984-1-tanmay.shah@amd.com](https://lore.kernel.org/r/20260204202730.3729984-1-tanmay.shah@amd.com)
- **The Bug**:
  When a list of SRAM phandles was provided, passing the phandle index `i` to `of_property_read_reg(sram_np, i, ...)` failed to parse the second phandle's address because each individual SRAM node's `reg` property starts at offset 0.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  Sunxi maps SRAM Space 0 and Space 1 directly by named resources (`r_sram` and `r_sram1`) with fixed hardware register properties, avoiding fragile dynamic phandle indexing.

---

### Case 4: Unbalanced IRQ Disabling on Error Rollback (`qcom_q6v5.c`)
- **Commit**: `110be46f5afe` ("remoteproc: qcom: q6v5: Avoid disabling handover IRQ twice")
- **Reviewer**: **Dmitry Baryshkov**, **Bjorn Andersson <andersson@kernel.org>**
- **Lore Link**: [https://lore.kernel.org/r/20250820-rproc-qcom-q6v5-fixes-v2-1-910b1a3aff71@linaro.org](https://lore.kernel.org/r/20250820-rproc-qcom-q6v5-fixes-v2-1-910b1a3aff71@linaro.org)
- **The Bug**:
  `enable_irq()` and `disable_irq()` are strictly reference-counted in the Linux IRQ core (`kernel/irq/manage.c`). If an error path invokes `disable_irq()` twice followed by a single `enable_irq()` on restart, the IRQ remains masked forever.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  Our crash interrupt uses atomic bitflags (`SUNXI_RPROC_CRASH_IRQ_ARMED`):
  ```c
  if (priv->crash_irq > 0 &&
      !test_and_set_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
      enable_irq(priv->crash_irq);
  ```
  And in `stop()`:
  ```c
  if (priv->crash_irq > 0 &&
      test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
      disable_irq(priv->crash_irq);
  ```
  Double-enables and double-disables are mathematically impossible.

---

### Case 5: Carveout Memory Clearing Corrupts Resume State (`imx_dsp_rproc.c`)
- **Commit**: `b490ddf27be2` ("remoteproc: imx_dsp_rproc: Only reset carveout memory at RPROC_OFFLINE state")
- **Lore Link**: [https://lore.kernel.org/r/20251218071750.2692132-1-shengjiu.wang@nxp.com](https://lore.kernel.org/r/20251218071750.2692132-1-shengjiu.wang@nxp.com)
- **Maintainer Signed-off-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **The Bug**:
  Blindly calling `memset(carveout->va, 0, len)` during `prepare()` or `load()` clears firmware data saved for system suspend/resume, causing resume failure.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  `memset_io()` in `prepare()` is explicitly confined to dedicated hardware SRAM Space 0 and switchable Space 1 (`priv->r_sram_va` and `priv->r_sram1_va`). DRAM carveouts are never unconditionally wiped.

---

### Case 6: Memory-Region Index Skipping Bug (`st_remoteproc.c`)
- **Commit**: `ac82dbc539c4` ("remoteproc: st: Fix indexing of memory-regions")
- **Author**: **Bjorn Andersson <bjorn.andersson@oss.qualcomm.com>**
- **Maintainer Signed-off-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **Lore Link**: [https://lore.kernel.org/r/20251126-st-remoteproc-double-index-v1-1-3b0a8b21ac18@oss.qualcomm.com](https://lore.kernel.org/r/20251126-st-remoteproc-double-index-v1-1-3b0a8b21ac18@oss.qualcomm.com)
- **The Bug**:
  Converting a `while` loop to a `for (i = 0; ...)` loop while leaving an extra `index++` inside the loop body skipped every other `memory-region` in DeviceTree.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  Single loop index `for (i = 0; i < count; i++)` in `sunxi_rproc_parse_memory_regions()` with zero secondary index variables.

---

### Case 7: Return Convention Bugs: `NULL` vs `ERR_PTR` (`imx_rproc`, `ti_k3`, `qcom`)
- **Commits**:
  - `665eebebb029` (`imx_rproc`: `devm_ioremap_resource_wc` returns `ERR_PTR`, not `NULL`)
  - `5b1f4b5c72cc` (`ti_k3`: `devm_ioremap_wc` returns `NULL`, not `ERR_PTR`)
  - `099a60cca1b8` (`imx_dsp_rproc`: `IS_ERR` check on `devm_ioremap_wc`)
- **The Bug**:
  Subtle confusion across kernel memory APIs:
  - `devm_ioremap()` / `devm_ioremap_wc()` $\rightarrow$ returns `NULL` on error.
  - `devm_ioremap_resource()` / `devm_platform_ioremap_resource()` $\rightarrow$ returns `ERR_PTR` on error.
  - `devm_memremap()` $\rightarrow$ returns `ERR_PTR` on error.
  Checking `!ptr` on an `ERR_PTR` lets negative addresses like `(void *)-12` pass through into `rproc_add_carveout()`, causing fatal kernel page faults.
- **Application to Sunxi (`sunxi_rproc.c`)**:
  Every pointer check strictly matches the exact API contract: `IS_ERR(priv->cfg_va)`, `IS_ERR(priv->r_sram_va)`, `!priv->remap_va`, and `IS_ERR(rmem->va)`.

---

## 2. Mailbox Subsystem Bug Fixes (`drivers/mailbox/`)

### Case 8: Sleep-in-Atomic in Mailbox `send_data` (`mtk-cmdq-mailbox.c`)
- **Commit**: `3f39f5652037` ("mailbox: mtk-cmdq: Remove pm_runtime APIs from cmdq_mbox_send_data()")
- **Lore Link**: [https://lore.kernel.org/r/20250829091558.17565-1-jason-jh.lin@mediatek.com](https://lore.kernel.org/r/20250829091558.17565-1-jason-jh.lin@mediatek.com)
- **Maintainer Signed-off-by**: **Jassi Brar <jassisinghbrar@gmail.com>**
- **The Bug**:
  Calling `pm_runtime_get_sync()` inside `cmdq_mbox_send_data()` triggered `BUG: sleeping function called from invalid context`.
  Root cause: `mbox_send_message()` invokes `chan->mbox->ops->send_data()` under `spin_lock_irqsave(&chan->lock)`.
- **Application to Sun55i (`sun55i-msgbox.c`)**:
  `sun55i_msgbox_send_data()` performs purely non-blocking MMIO FIFO writes under spinlock protection. All power management and clock control is handled outside the mailbox transfer path.

---

### Case 9: Shared SGI / Channel Memory Leak on Module Unbind (`zynqmp-ipi.c`)
- **Commit**: `bb160e791ab1` ("mailbox: zynqmp-ipi: Fix SGI cleanup on unbind")
- **Commit**: `0aead8197fc1` ("mailbox: zynqmp-ipi: Fix out-of-bounds access in mailbox cleanup loop")
- **The Bug**:
  During driver unbind/remove, freeing channels in reverse order without unmasking peripheral interrupts caused pending interrupts to trigger on a partially dismantled controller structure.
- **Application to Sun55i (`sun55i-msgbox.c`)**:
  Controller uses `devm_mbox_controller_register()`. On detach, devres unwinds in strict reverse order: unregisters the mailbox controller first (preventing new messages), disables all channel IRQs, and finally tears down the hardware bank mappings.

---

## 3. Subsystem Master Reviewer Pattern Matrix

| Reviewer / Maintainer | Subsystem | Historical Rejection Issue Caught | Proven Upstream Safeguard |
| :--- | :--- | :--- | :--- |
| **Mathieu Poirier** | RemoteProc | Workqueue races with VirtIO subdevice removal. | Guard work item with `rproc->lock` and `rproc->state == RPROC_RUNNING`. |
| **Mathieu Poirier** | RemoteProc | Re-reserving memory resources on second start causes `-EBUSY`. | Map once in `probe()`; only attach carveout pointers in `.prepare()`. |
| **Bjorn Andersson** | RemoteProc | Duplicate loop index variable skips alternate DT memory regions. | Use a single clean `for (i = 0; i < count; i++)` loop. |
| **Bjorn Andersson** | RemoteProc | `enable_irq`/`disable_irq` reference count imbalance disables IRQ permanently. | Use atomic bit operations (`test_and_set_bit`) to gate IRQ enables. |
| **Jassi Brar** | Mailbox | Sleeping functions (`pm_runtime_get_sync`) called inside `send_data()`. | Keep `send_data()` atomic and lockless; delegate PM to clients. |
| **Jassi Brar** | Mailbox | `last_tx_done` returning `true` on buffer capacity instead of complete drain. | Require `count == 0` so VirtIO descriptors are not overwritten. **Superseded:** the project uses `count < 8` (FIFO has room) and `send_data()` refuses a full FIFO — `DESIGN_DECISIONS.md` "Message box TX completion". The `count == 0` request came from the v2 Sashiko review (UPSTREAM_LESSONS_LEARNED Lesson 2.1); this Jassi Brar attribution is unverified. |
| **Krzysztof Kozlowski** | DeviceTree | `enum:` across multiple array items in `reg-names` or `clock-names`. | Enforce positional items with `- const:` only. |
