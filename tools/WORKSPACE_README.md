# Allwinner T527 & A733 Flight Controller Workspace

Top-level workspace for the **Radxa Cubie A5E** (Allwinner T527/A527) and **Radxa Cubie A7A / A7Z** (Allwinner A733) flight controller distributions and mainline Linux bring-up.

---

## 🚀 Multi-PC Development & Quick Start

### 1. New Machine Setup (Run once on any PC)
```bash
git clone git@github.com:tcmichals/cubie-a5e.git
./cubie-a5e/tools/setup_workspace.sh
```
*Automatically clones `linux-cubie` (`cubie-linux-7.1`), clones `buildroot`, configures `local.mk` (`LINUX_OVERRIDE_SRCDIR`), and initializes `bld.a5e` and `bld.a7a`.*

---

### 2. Multi-PC Sync Helper: `tools/sync_kernel.sh`

```bash
# Check status across both cubie-a5e and linux-cubie:
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

## 📂 Workspace Structure

* **[`cubie-a5e/`](cubie-a5e/)**: Main project repository containing Buildroot external tree (`project-cubie-a5e`), RISC-V firmware apps, test suite, and technical documentation.
  * **[`cubie-a5e/README.md`](cubie-a5e/README.md)**: Full platform specifications, memory maps, and architecture guide.
  * **[`cubie-a5e/tools/setup_workspace.sh`](cubie-a5e/tools/setup_workspace.sh)**: Automated multi-machine workspace setup.
  * **[`cubie-a5e/tools/sync_kernel.sh`](cubie-a5e/tools/sync_kernel.sh)**: Kernel push/pull/rebuild sync tool.
* **[`linux-cubie/`](linux-cubie/)**: Standalone Linux kernel Git tree (tracking `cubie-linux-7.1` and `linux-next`) for active driver development and upstream RFC preparation.
* **[`buildroot/`](buildroot/)**: Upstream Buildroot repository.
* **`bld.a5e/`**: Build output directory for Radxa Cubie A5E (configured via `cubie_a5e_defconfig` + `local.mk`).
* **`bld.a7a/`**: Build output directory for Radxa Cubie A7A (configured via `cubie_a7a_defconfig` + `local.mk`).
