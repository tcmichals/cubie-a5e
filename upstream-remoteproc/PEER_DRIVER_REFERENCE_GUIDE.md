# Upstream RemoteProc & Mailbox Peer Driver Reference Guide

This document catalogs accepted mainline Linux kernel patterns across peer RemoteProc and Mailbox drivers, detailing how the **Allwinner sun55i (A523/A527/T527) XuanTie E906 RemoteProc and Message Box drivers** align with upstream subsystem conventions.

---

## 1. RemoteProc Subsystem Reference Comparison

### Peer Matrix

| Architectural Pattern | Allwinner `sunxi_rproc.c` | NXP i.MX `imx_rproc.c` | ST STM32 `stm32_rproc.c` | TI K3 `ti_k3_common.c` | Upstream Subsystem Invariant |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Address Translation (ATT)** | `sunxi_rproc_att` (SRAM Space 0 & 1) | `imx_rproc_att` (TCM & internal SRAM) | Fixed hardware windows | Core `da_to_va` hooks | Hardware local core addresses (DA) must be translated to host physical addresses (PA) via explicit tables. |
| **Carveout Registration** | Registered in `.prepare()` via `rproc_add_carveout()` | Fixed reserved-memory or CMA | Registered in `.prepare()` | Reserved-memory pool | RemoteProc core's `rproc_resource_cleanup()` frees carveouts on stop; registering in `.prepare()` survives restarts. |
| **Memory-Region Layout** | Positional: index 0 is DMA pool (`vram`), then `dram`, `trace` | Free-form reserved-memory names | Name-based lookup (`vdev0buffer`, `vdev0vring`) | Positional: index 0 is DMA pool for virtio | Positional layout satisfies Krzysztof Kozlowski (KK8) and standard `remoteproc_virtio.c` fallback. |
| **Mailbox RX Notification** | Defer to ordered workqueue (`priv->workqueue`) | Defer to ordered workqueue (`priv->workqueue`) | Direct / Threaded callback | Mailbox rx callback | Mailbox `rx_callback` is atomic/hardirq; RPMsg receive path can sleep. Deferral to process context is mandatory. |
| **Teardown Order** | `rproc_del()` $\rightarrow$ `mbox_free()` $\rightarrow$ `cancel_work_sync()` | `rproc_del()` $\rightarrow$ free channels | `rproc_del()` $\rightarrow$ free channels | `rproc_del()` $\rightarrow$ free channels | Remote processor and virtio devices must halt before communication channels are severed. |
| **Posted-Write Flushes** | Explicit `readl()` flush after `STA_ADD` and `REMAP` | Explicit `readl()` flushes | Register readback | Register readback | ARM64 weakly ordered interconnects require explicit dummy reads before hardware reset deassertion. |

### Key Architectural Learnings from Peers

1. **Address Translation Table (ATT) vs Carveouts**:
   - `imx_rproc.c` maps all memory (TCM, SRAM, and DRAM) through its internal ATT table.
   - `sunxi_rproc.c` delegates DRAM, trace buffers, and VirtIO vrings directly to the RemoteProc core framework (`rproc_da_to_va()`), mapping only on-chip dedicated SRAM Space 0 and Space 1 in `sunxi_rproc_da_to_va()`. This avoids duplicate address-lookup logic and keeps driver translation minimal.

2. **Workqueue Deferral for Mailbox RX**:
   - Mailbox clients run in atomic context with spinlocks held in hardirq.
   - `imx_rproc.c` solves the sleep-in-atomic problem by queuing all incoming virtqueue notifications to an ordered workqueue (`imx_rproc_vq_work`).
   - `sunxi_rproc.c` matches this exact pattern with `sunxi_rproc_vq_work`, ensuring standard non-RT kernels with `CONFIG_DEBUG_ATOMIC_SLEEP=y` execute `rproc_vq_interrupt()` without warnings.

---

## 2. Mailbox Subsystem Reference Comparison

### Peer Matrix

| Architectural Pattern | Allwinner `sun55i-msgbox.c` | Allwinner Legacy `sun6i-msgbox.c` | ARM MHU `arm_mhu.c` | BCM2835 `bcm2835-mailbox.c` | Upstream Subsystem Invariant |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Topology** | 4-port, 12-channel full mesh crossbar | 2-port, 8-channel point-to-point | Multi-channel point-to-point | Single FIFO doorbells | Sun55i links 4 heterogeneous cores (ARM, DSP, CPUS, RISC-V); each sender writes to receiver's bank. |
| **Pacing (`last_tx_done`)**| `count < 8` (FIFO has room); DT selects timer or write IRQ (enabled only while full) — DESIGN_DECISIONS 2026-10-16 | remote IRQ status, 5 ms poll | Hardware ACK | FIFO empty bit | A doorbell is done once the FIFO accepted it. `count == 0` with a 1 ms poll overflowed the 20-slot core queue in bursts (measured 2026-10-16). Precedent for a DT TX-mode switch: `ti,mbox-send-noirq` (OMAP). |
| **Pending Clear Timing** | Drain FIFO, then clear (fixed 2026-10-16) | Drain, then clear | Auto-cleared by hardware | Auto-cleared by hardware | The pending bit follows "FIFO not empty" and ignores a clear while a message waits: clearing first raised 2 IRQs per message (measured, D13). A message arriving after the clear sets it again. |
| **Drain Loop Cap** | `for (i = 0; i < FIFO_MAX; i++)` | Bounded drain loop | Single word | Single word | Unbounded loops in hardirq top-half can cause CPU soft-lockups if remote processor spams FIFO. |
| **Clocks & Resets** | Host bank only (`CLK_BUS_MSGBOX`) | Single SoC clock/reset | AMBA bus clocks | Bus clocks | Linux must not claim or reset coprocessor hardware banks on unbind. |

---

## 3. DeviceTree Binding Schema Peer Patterns

| Property | `allwinner,sun55i-a523-rproc.yaml` | `st,stm32-rproc.yaml` | `fsl,imx-rproc.yaml` | Maintainer Standard |
| :--- | :--- | :--- | :--- | :--- |
| **`reg-names`** | Fixed positional `items: [ - const: ... ]` | Fixed positional `items:` | Fixed positional `items:` | Krzysztof: `enum:` across multiple items is strictly forbidden. |
| **`mboxes`** | Single bidirectional channel (`maxItems: 1`) | Multi-channel (`minItems: 1`) | Dual channel | Hardware-accurate channel count; omit unused dummy channels. |
| **`power-domains`** | Positional `items: [ - const: riscv, - const: sram ]` | Single or power-domain list | Single power domain | All required hardware power domains must be explicitly defined. |
| **`additionalProperties`** | `false` | `false` | `false` | Mandatory across all non-inheriting DT schemas. |
