# Sashiko-Grade Multi-Stage Adversarial Review Report

**Audit Status**: FLAGGED (2 Issues Detected)

---

## Review Stage Breakdown
- **Stage 1 (Hardirq & Concurrency)**: Evaluated SMP lock protection, TOCTOU windows, and loop boundedness.
- **Stage 2 (Resource Lifecycle & Teardown)**: Verified probe error symmetry, teardown order, and UAF hazards.
- **Stage 3 (Subsystem Framework Contracts)**: Audited Mailbox pacing and RemoteProc ATT address translation.
- **Stage 4 (Interconnect, MMIO & Endianness)**: Checked posted-write read-backs and Big-Endian mock accessors.
- **Stage 5 (Adversarial Gatekeeper)**: Deduplicated and validated findings against kernel subsystem constraints.

---

### ⚠️ Flagged Issues Requiring Resolution

| Stage | Severity | ID | File | Finding Description |
|:---:|:---:|:---:|:---|:---|
| Stage 3 | **High** | `M2` | `sun55i-msgbox.c` | Broken last_tx_done polling condition |
| Stage 4 | **Medium** | `K2` | `sunxi_rproc_test.c` | Direct mock register access breaks on Big-Endian |


#### [High] Broken last_tx_done polling condition (`M2`)
- **File**: `drivers/mailbox/sun55i-msgbox.c`
- **Explanation**: last_tx_done() must return true only when count == 0 (remote consumed), not on available FIFO space.

#### [Medium] Direct mock register access breaks on Big-Endian (`K2`)
- **File**: `drivers/remoteproc/sunxi_rproc_test.c`
- **Explanation**: writel() byte-swaps on Big-Endian; assertions must read via readl() rather than array indices.

