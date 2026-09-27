# Kernel Branch Discipline: Board Development vs Upstream Patch RFCs

This rule defines the strict branch separation required when alternating between board system integration and upstream Linux kernel patch submissions.

---

## 1. Board Development Rule (Radxa Cubie A5E & Cubie A7A)

Whenever working on board bring-up, peripheral drivers, device trees, Buildroot packages, or board image builds (`bld.a5e` and `bld.a7a`):

* **The active branch in `linux-cubie` MUST be `cubie-linux-7.1`.**
* Both `bld.a5e/local.mk` and `bld.a7a/local.mk` use:
  ```make
  LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../linux-cubie
  ```
* Both hardware targets coexist in `cubie-linux-7.1`:
  - Cubie A5E (Allwinner T527/A527): `arch/arm64/boot/dts/allwinner/sun55i-a527-cubie-a5e.dts`
  - Cubie A7A (Allwinner A733): `arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts`
  - All integrated peripheral drivers (CCU, PRCM, pinctrl, USB, PHY, ethernet, remoteproc).
* **Never attempt to compile `bld.a5e` or `bld.a7a` while `linux-cubie` is on an upstream submission branch.** Building `bld.a7a` against an upstream-only branch will fail because `sun60i-a733-cubie-a7a.dtb` only exists on `cubie-linux-7.1`.

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
  - Switch `linux-cubie` back to `cubie-linux-7.1`:
    ```bash
    git -C /home/tcmichals/projects/cubie/linux-cubie checkout cubie-linux-7.1
    ```

---

## 3. Quick Verification

Before running `make -C bld.a7a` or `make -C bld.a5e`, verify the active branch:
```bash
./cubie-a5e/tools/sync_kernel.sh status
```
Confirm `[2] linux-cubie status:` shows `## cubie-linux-7.1...origin/cubie-linux-7.1`.
