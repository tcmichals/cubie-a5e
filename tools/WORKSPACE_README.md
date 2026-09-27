# Allwinner T527 & A733 Flight Controller Workspace

Top-level workspace for the **Radxa Cubie A5E** (Allwinner T527/A527) and **Radxa Cubie A7A / A7Z** (Allwinner A733) flight controller distributions and mainline Linux bring-up.

---

## 🚀 Multi-PC Development & Quick Start

### 1. New Machine Setup (Run once on any PC)
```bash
git clone git@github.com:tcmichals/cubie-a5e.git
./cubie-a5e/tools/setup_workspace.sh
```
* Automatically clones `linux-cubie` (`cubie-linux-7.1`), clones `buildroot`, configures `local.mk` (`LINUX_OVERRIDE_SRCDIR`) for both targets, and initializes `bld.a5e` and `bld.a7a`.*

---

### 2. Multi-PC Sync Helper: `tools/sync_kernel.sh`

```bash
# Check status across cubie-a5e and linux-cubie:
./cubie-a5e/tools/sync_kernel.sh status

# Push kernel commits to GitHub before switching PCs:
./cubie-a5e/tools/sync_kernel.sh push

# Pull latest kernel commits and rebuild on another PC:
./cubie-a5e/tools/sync_kernel.sh pull

# Force an incremental kernel rebuild across both targets:
./cubie-a5e/tools/sync_kernel.sh rebuild
```

---

## 🛠️ Build Commands

| Target Board | Full Image Build | Incremental Kernel Rebuild | Output Image |
|---|---|---|---|
| **Radxa Cubie A5E** (T527) | `make -C bld.a5e` | `make -C bld.a5e linux-rebuild` | `bld.a5e/images/sdcard.img` |
| **Radxa Cubie A7A** (A733) | `make -C bld.a7a` | `make -C bld.a7a linux-rebuild` | `bld.a7a/images/sdcard.img` |

---

## 📬 Upstream Kernel Submissions & Sashiko Review Protocols

Active driver upstreaming for the Allwinner sun55i/sun60i RemoteProc and Message Box drivers is tracked in [`cubie-a5e/upstream-remoteproc/`](cubie-a5e/upstream-remoteproc/):

* **[Upstream Series Hub](cubie-a5e/upstream-remoteproc/README.md)**: Patch series (RFC v1, v2, v3), cover letters, and Lore threads.
* **[Sashiko Adversarial Review Protocols](cubie-a5e/upstream-remoteproc/reviews/sashiko_protocols.md)**: Linux Foundation Sashiko-grade multi-stage adversarial kernel review prompts:
  - **Stage 1**: Hardirq & SMP Concurrency Auditor (`stage1_concurrency.md`)
  - **Stage 2**: Resource Lifecycle & Teardown Symmetry (`stage2_lifecycle.md`)
  - **Stage 3**: Subsystem Framework Contracts (`stage3_contracts.md`)
  - **Stage 4**: Interconnect Ordering & Endianness (`stage4_interconnect.md`)
  - **Stage 5**: Adversarial Gatekeeper & Triage (`gatekeeper.md`)
* **[Review Tracker & Resolution Matrix](cubie-a5e/upstream-remoteproc/reviews/REVIEW_TRACKER.md)**: Issue tracking and verification across maintainer feedback and `sashiko-bot`.

---

## 📂 Workspace Structure

* **[`cubie-a5e/`](cubie-a5e/)**: Main project repository containing Buildroot external tree (`project-cubie-a5e`), RISC-V firmware apps, test suite, and technical documentation.
  * **[`cubie-a5e/README.md`](cubie-a5e/README.md)**: Full platform specifications, memory maps, and architecture guide.
  * **[`cubie-a5e/upstream-remoteproc/`](cubie-a5e/upstream-remoteproc/)**: Upstream patch series, Lore discussion archives, and [Sashiko review protocols](cubie-a5e/upstream-remoteproc/reviews/sashiko_protocols.md).
  * **[`cubie-a5e/tools/setup_workspace.sh`](cubie-a5e/tools/setup_workspace.sh)**: Automated multi-machine workspace setup.
  * **[`cubie-a5e/tools/sync_kernel.sh`](cubie-a5e/tools/sync_kernel.sh)**: Kernel push/pull/rebuild sync tool.
* **[`linux-cubie/`](linux-cubie/)**: Standalone Linux kernel Git tree (tracking `cubie-linux-7.1`), shared by both Cubie A5E and Cubie A7A.
* **[`buildroot/`](buildroot/)**: Upstream Buildroot repository.
* **`bld.a5e/`**: Build output directory for Radxa Cubie A5E (configured via `cubie_a5e_defconfig` + `local.mk`).
* **`bld.a7a/`**: Build output directory for Radxa Cubie A7A (configured via `cubie_a7a_defconfig` + `local.mk`).
