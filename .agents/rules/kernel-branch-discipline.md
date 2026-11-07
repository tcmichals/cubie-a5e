# Kernel Branch Discipline: Board Development vs Upstream Patch RFCs

This rule defines the strict branch separation required when alternating between board system integration and upstream Linux kernel patch submissions.

---

## 1. Board Development Rule (Radxa Cubie A5E & Cubie A7A)

Whenever working on board bring-up, peripheral drivers, device trees, Buildroot packages, or board image builds (`bld.a5e` and `bld.a7a`):

* **Each board builds from its own kernel tree and branch:**

  | Board | `LINUX_OVERRIDE_SRCDIR` in `bld.<board>/local.mk` | Branch |
  |---|---|---|
  | Cubie A5E | `$(TOPDIR)/../linux-v4-71` (git worktree of `linux-cubie`) | `cubie-linux-7.1` |
  | Cubie A7A | `$(TOPDIR)/../linux-cubie` | `feature/a733-e902-remoteproc` |

* **The split is deliberate.** `cubie-linux-7.1` stays clean for upstream work. A7A camera (CSI, MIPI D-PHY) and A733 E902 remoteproc support exist only on the feature branch; the v4 remoteproc and mailbox fixes exist only on `cubie-linux-7.1` (checked 2026-11-06: 7 and 36 commits). Merging them conflicts in `sunxi_rproc.c` and `sunxi_rproc.h`.
* **`local.mk` is not in git.** Keep a copy before deleting a build directory; `tools/setup_workspace.sh` writes the values above.
* **Never build `bld.a7a` with `linux-cubie` on any other branch**: the board loses its camera and E902 remoteproc support. The build does not fail on `cubie-linux-7.1`, because `sun60i-a733-cubie-a7a.dts` exists there too.

---

## 2. Upstream RFC Submission Rule (RemoteProc & Mailbox Series)

Whenever working on upstream Linux kernel mailing list submissions (`linux-remoteproc@vger.kernel.org`, `linux-sunxi@lists.linux.dev`), maintainer reviews, or Sashiko protocol audits:

* Switch `linux-cubie` to the designated upstream series branch (e.g., `v3-sun55i-rproc-msgbox` or `linux-next`).
* **Before switching:**
  1. Verify working directory is clean: `git -C linux-cubie status`.
  2. Commit or stash any in-flight changes.
* In the upstream branch:
  - Run KUnit test suites (`tools/testing/kunit/kunit.py run`).
  - Run `scripts/checkpatch.pl --strict`.
  - Export patches to `cubie-a5e/upstream-remoteproc/vX/`.
* **Before returning to board builds or A7A/A5E development:**
  - Switch `linux-cubie` back to the A7A branch:
    ```bash
    git -C /home/tcmichals/projects/cubie/linux-cubie checkout feature/a733-e902-remoteproc
    ```

---

## 3. Quick Verification

Before running `make -C bld.a7a` or `make -C bld.a5e`, verify the active branch:
```bash
./cubie-a5e/tools/sync_kernel.sh status
```
Confirm `[2]` shows `feature/a733-e902-remoteproc` and `[3]` shows `cubie-linux-7.1`, with no `WARNING: expected branch` line.
