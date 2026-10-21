# Mainline Linux Peer Driver Reference Guide

This reference document establishes the architectural benchmarks for the **Allwinner sun55i (A523/A527/T527) RemoteProc and Message Box drivers** by cross-referencing accepted upstream drivers in the Linux kernel tree.

---

## 1. RemoteProc Driver Peer Matrix

| Architectural Feature | Allwinner `sunxi_rproc.c` | NXP i.MX `imx_rproc.c` | ST STM32 `stm32_rproc.c` | TI K3 `ti_k3_common.c` | Upstream Rule / Rationale |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Address Translation (ATT)** | `sunxi_rproc_att` (SRAM Space 0 & Space 1) | `imx_rproc_att` (TCM & internal SRAM) | Fixed memory windows | Core `da_to_va` hooks | Hardware local core addresses (DA) must be translated to host physical addresses (PA) via explicit tables. |
| **DRAM / Trace Carveouts** | Registered in `.prepare()` via `rproc_add_carveout()` | Fixed reserved-memory or CMA | Registered in `.prepare()` | Reserved-memory pool | RemoteProc core's `rproc_resource_cleanup()` frees carveouts on stop; registering in `.prepare()` survives restarts. |
| **Memory-Region Layout** | Positional: index 0 is DMA pool (`vram`), then `dram`, `trace` | Free-form reserved-memory names | Name-based lookup (`vdev0buffer`, `vdev0vring`) | Positional: index 0 is DMA pool for virtio | Positional layout satisfies Krzysztof Kozlowski (KK8) and standard `remoteproc_virtio.c` fallback. |
| **Mailbox RX Notification** | Defer to ordered workqueue (`priv->workqueue`) | Defer to ordered workqueue (`priv->workqueue`) | Direct / Threaded callback | Mailbox rx callback | Mailbox `rx_callback` is atomic/hardirq; RPMsg receive path can sleep. Deferral to process context is mandatory. |
| **Teardown Order** | `rproc_del()` $\rightarrow$ `mbox_free()` $\rightarrow$ `cancel_work_sync()` | `rproc_del()` $\rightarrow$ free channels | `rproc_del()` $\rightarrow$ free channels | `rproc_del()` $\rightarrow$ free channels | Remote processor and virtio devices must halt before communication channels are severed. |
| **Posted-Write Flushes** | Explicit `readl()` flush after `STA_ADD` and `REMAP` | Explicit `readl()` flushes | Register readback | Register readback | ARM64 weakly ordered interconnects require explicit dummy reads before hardware reset deassertion. |

---

## 2. Mailbox Driver Peer Matrix

| Feature | Allwinner `sun55i-msgbox.c` | Allwinner Legacy `sun6i-msgbox.c` | ARM MHU `arm_mhu.c` | BCM2835 `bcm2835-mailbox.c` | Upstream Rule / Rationale |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Topology** | 4-port, 12-channel full mesh crossbar | 2-port, 8-channel point-to-point | Multi-channel point-to-point | Single FIFO doorbells | Sun55i links 4 heterogeneous cores (ARM, DSP, CPUS, RISC-V); each sender writes to receiver's bank. |
| **Pacing (`last_tx_done`)**| `count < 8` (FIFO has room); DT selects 1 ms poll (default) or the write IRQ (`allwinner,tx-done-irq`, armed after every send; E906 bank on SPI 205, msgbox reference §6b) | remote IRQ status, 5 ms poll | Hardware ACK | FIFO empty bit | Owner decision 2026-10-16. `count == 0` with a 1 ms poll overflowed the 20-slot core queue in bursts. |
| **Pending Clear Timing** | Drain FIFO, then clear (D13) | Drain, then clear ("can be cleared only once the FIFO is empty") | Auto-cleared by hardware | Auto-cleared by hardware | The pending bit follows "FIFO not empty" and ignores a clear while a message waits: clearing first raised 2 IRQs per message (measured, D13). A message arriving after the clear sets it again. |
| **Drain Loop Cap** | `for (i = 0; i < FIFO_MAX; i++)` | Bounded drain loop | Single word | Single word | Unbounded loops in hardirq top-half can cause CPU soft-lockups if remote processor spams FIFO. |
| **Clocks & Resets** | All four banks' bus clocks and resets (D12, reopened 2026-10-16) | Single SoC clock/reset | AMBA bus clocks | Bus clocks | Each bank has its own clock and reset; with only the ARM bank's, the RV bank stays gated and doorbells never reach the E906 (measured, msgbox reference §6). |

---

## 3. DeviceTree Binding Schema Peer Matrix

| Property | `sun55i-a523-rproc.yaml` | `st,stm32-rproc.yaml` | `fsl,imx-rproc.yaml` | Maintainer Standard |
| :--- | :--- | :--- | :--- | :--- |
| **`reg-names`** | Fixed positional `items: [ - const: ... ]` | Fixed positional `items:` | Fixed positional `items:` | Krzysztof: `enum:` across multiple items is strictly forbidden. |
| **`mboxes`** | Single bidirectional channel (`maxItems: 1`) | Multi-channel (`minItems: 1`) | Dual channel | Hardware-accurate channel count; omit unused dummy channels. |
| **`power-domains`** | Positional `items: [ - const: riscv, - const: sram ]` | Single or power-domain list | Single power domain | All required hardware power domains must be explicitly defined. |
| **`additionalProperties`** | `false` | `false` | `false` | Mandatory across all non-inheriting DT schemas. |
