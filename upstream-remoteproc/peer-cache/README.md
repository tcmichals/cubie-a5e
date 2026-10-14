# Mainline Peer Drivers & Interface Contracts Cache

This directory caches upstream mainline Linux kernel drivers, DeviceTree binding schemas, and subsystem headers. It serves as an immediate, local reference for code reviews, architectural comparisons, and subsystem contract verification.

---

## Cached Components & Reference Purposes

| File | Subsystem | Upstream Reference Purpose |
| :--- | :--- | :--- |
| **[`imx_rproc.c`](imx_rproc.c)** | RemoteProc | **NXP i.MX Reference**: Address Translation Table (ATT) architecture, core-to-bus address mapping, and ordered workqueue notification handling. |
| **[`stm32_rproc.c`](stm32_rproc.c)** | RemoteProc | **ST STM32 Reference**: Dynamic carveout lifecycle in `.prepare()`, auto-boot, power sequencing, and clean resource cleanup. |
| **[`ti_k3_common.c`](ti_k3_common.c)** | RemoteProc | **TI K3 Reference**: Positional `memory-region` handling (index 0 DMA pool), reset management, and safe shutdown. |
| **[`sun6i-msgbox.c`](sun6i-msgbox.c)** | Mailbox | **Allwinner Legacy Reference**: Predecessor Allwinner hardware msgbox architecture, register offsets, and FIFO status polling. |
| **[`remoteproc.h`](remoteproc.h)** | Core API | Authoritative struct definitions: `struct rproc_ops`, `struct rproc_mem_entry`, `struct rproc`. |
| **[`mailbox_client.h`](mailbox_client.h)** | Core API | Authoritative contract for `mbox_client` (explicit documentation of atomic context in `rx_callback`). |
| **[`mailbox_controller.h`](mailbox_controller.h)** | Core API | Authoritative contract for `mbox_controller` and `mbox_chan_ops`. |
| **[`st,stm32-rproc.yaml`](st,stm32-rproc.yaml)** | DT Schema | Mainline coprocessor binding schema reference. |
| **[`allwinner,sun6i-a31-msgbox.yaml`](allwinner,sun6i-a31-msgbox.yaml)** | DT Schema | Mainline Allwinner mailbox binding schema reference. |

---

## Refreshing This Cache

To update these files against the latest `linux-cubie` commit:
```bash
python3 scripts/update_peer_cache.py
```
