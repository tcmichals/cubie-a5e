# Upstream Review & Feedback Tracker (v2 -> v3)

- **Last Updated**: `2026-09-27 01:21:49 UTC`
- **Lore Mailing List Thread**: [https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/)
- **Sashiko AI Review Dashboard**: [https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com](https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com)

---

## 1. Quick Ingestion Instructions

When new review feedback arrives:
1. Save the email or paste the review text as a file into `upstream-remoteproc/lore_emails/<message-id>.eml` or `reviews/incoming_emails/review_<name>.txt`.
2. Run `python3 scripts/fetch_lore_reviews.py`.
3. The script will automatically parse tags, findings, and update the review resolution table.

---

## 2. Ingested Email Reviews

*(No external email files ingested in `lore_emails/` or `incoming_emails/` yet. Seeded with v2 initial feedback)*

---

## 3. Review Resolution Status Matrix (v2 -> v3)

| ID | Component | Severity | Issue / Finding | Status in v3 |
|:---|:---|:---:|:---|:---:|
| **M1** | `sun55i-msgbox.c` | **High** | Unbounded DT irq_cnt causes array overflow | **FIXED** |
| **M2** | `sun55i-msgbox.c` | **High** | Broken last_tx_done polling condition (count == 0) | **FIXED** |
| **M3** | `sun55i-msgbox.c` | **High** | NULL pointer deref in IRQ handler during teardown | **FIXED** |
| **M4** | `sun55i-msgbox.c` | **High** | Lockless multi-IRQ concurrency / TOCTOU race (SMP spinlock) | **FIXED** |
| **T1** | `drivers/mailbox/Kconfig` | **Low** | Missing SUN55I_MSGBOX dependency for KUnit tests | **FIXED** |
| **T2** | `sun55i_msgbox_test.c` | **Medium** | MMIO endianness bug in mock registers on Big-Endian | **FIXED** |
| **T3** | `sun55i_msgbox_test.c` | **Low** | Mock bypass causes startup() flush test to silently succeed | **FIXED** |
| **R1** | `sunxi_rproc.c` | **High** | Unbalanced disable_irq via crash_irq_enabled race | **FIXED** |
| **R2** | `sunxi_rproc.c` | **High** | Race on kick_msg and immediate txdone (switched to stack local) | **FIXED** |
| **R3** | `sunxi_rproc.c` | **High** | Double mapping of DT regions (WB vs WC attributes conflict) | **FIXED** |
| **R4** | `sunxi_rproc.c` | **High** | Premature core execution due to broken reset fallback | **FIXED** |
| **R5** | `sunxi_rproc.c` | **High** | UAF of virtqueues due to late mailbox interrupts in remove | **FIXED** |
| **R6** | `sunxi_rproc.c` | **High** | UAF of priv in probe error path due to workqueue teardown | **FIXED** |
| **R7** | `sunxi_rproc.c` | **High** | UAF of rproc in remove due to crash_irq_enabled data race | **FIXED** |
| **R8** | `sunxi_rproc.c` | **Medium** | da_to_va translates unmatched ATT addresses as host PAs | **FIXED** |
| **R9** | `sunxi_rproc.c` | **Medium** | Missing teardown of crash IRQ on start failure leaks state | **FIXED** |
| **R10** | `sunxi_rproc.c` | **Medium** | Missing write flush of boot address causes execution race | **FIXED** |
| **K1** | `drivers/remoteproc/Kconfig` | **Low** | Missing SUNXI_REMOTEPROC dependency in Kconfig | **FIXED** |
| **K2** | `sunxi_rproc_test.c` | **Medium** | KUnit test mock MMIO reads fail on Big-Endian | **FIXED** |
| **K3** | `sunxi_rproc_test.c` | **Medium** | False positive KUnit test for obsolete kick_msg field | **FIXED** |

---
