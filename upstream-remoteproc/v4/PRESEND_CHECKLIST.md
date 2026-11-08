# v4 Pre-Send Checklist

Tick each line with the date, commit and result. A box without evidence is not done.

## 1. Build (on the linux-next base, each patch on its own)

**Folded series:** local branch `v4-next-20261105` in `linux-v4-next`, not pushed. It is v3 `b47352fa3f97`
rebased on next-20261105, with each patch's files set to `cubie-linux-7.1` `da2fdad67e0d` and the v4
messages applied (V4-CHLOG). 2026-11-07, at each patch on its own, GCC 15.1 and clang 21.1 W=1 with the
drivers and KUnit as modules: 0 driver warnings. At the tip: `dt_binding_check` clean, `dtbs_check`
on the A5E clean, UML KUnit 65 pass and 1 skip. `checkpatch --strict` shows only the MAINTAINERS note on 1, 3, 6
and 7 (the entries are added with the drivers in 2 and 4) and the missing Signed-off-by. `spdxcheck`
did not run: no `ply` module on this host.

- [x] `W=1`, GCC and clang, built in and as modules — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): GCC 15.1 (Buildroot) and clang 21.1.8, `y` and `m` (both KUnit suites as modules, modpost run), 0 driver warnings. allnoconfig + only the drivers' dependencies
- [x] `C=1` sparse clean on the new files (missed in v3: V3-S8). Check the log has `CHECK` lines: an
      old sparse (Ubuntu 0.6.4) is rejected by `scripts/checker-valid.sh` and `C=` is silently ignored — 2026-11-07, sparse v0.6.5-rc1-21-g84ec0658 built from git (`~/projects/cubie/sparse`; Ubuntu 0.6.4 is rejected by `checker-valid.sh`), `C=2` on `c22242e478f5` (same files as the fold), arm64, all four as modules: `CHECK` lines for `sun55i-msgbox.c`, `sun55i_msgbox_kunit.c`, `sunxi_rproc.c`, `sunxi_rproc_kunit.c`, no warnings (the only one is `arch/arm64/kernel/vdso/vgetrandom.c`, not ours)
- [x] `CONFIG_PM=n` build, drivers and KUnit (missed in v3: V3-S7) — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): GCC W=1, 0 warnings; msgbox KUnit builds, rproc KUnit is off (it `depends on PM`)
- [x] `CONFIG_COMPILE_TEST` on a non-arm64 arch — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): 32-bit ARM, clang W=1, `ARCH_SUNXI` off, drivers and KUnit as modules, 0 warnings. x86_64 not run: objtool needs `libelf-dev` on this host
- [x] `make dt_binding_check DT_SCHEMA_FILES=...` for both bindings, and `dtbs_check` — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): `dt_binding_check` clean for both; `CHECK_DTBS=y sun55i-a527-cubie-a5e.dtb` (msgbox and rproc nodes present) has one warning, `gpu-trips` in thermal-zones, from the thermal series (`2ac0cd62a627`), not ours
- [x] `checkpatch.pl --strict` on every patch — re-run 2026-11-08 after the L-1 … L-11 cleanups and the binding text (tip `6250bc6f4df3`): per patch GCC + clang W=1 modules exit 0, 0 driver warnings; checkpatch only the MAINTAINERS note on 1, 3, 6, 7; dt_binding_check (both), dtbs_check (A5E) clean; UML KUnit 66 run, 65 pass / 1 skip. Before that, folded series re-checked 2026-11-07 after the KUnit fix was folded into 6/7 (tip `3eedd6a66aea`): per patch GCC + clang W=1 modules exit 0, 0 driver warnings; checkpatch only the MAINTAINERS note on 1, 3, 6, 7; dt_binding_check, dtbs_check clean; UML KUnit 65 pass / 1 skip. Before that, folded series 2026-11-07: clean apart from the notes above (blank lines left by the crash IRQ removal fixed in `da2fdad67e0d`). Earlier: 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold), on the unfolded commits: only "Missing Signed-off-by" (owner adds it), plus 2 over-100-column lines in `70509dc4e6fe` and a long changelog line in `4931baf9d66b`, both gone once the fixups are folded. Re-run on the folded series

## 2. Tests

- [x] KUnit: both suites built in and as modules, and under UML (`kunit.py run`) — built in on the board 2026-11-07 (`da2fdad67e0d`, 7.1 PREEMPT_RT, image md5 4d7089db): sun55i_msgbox 37/37, sunxi_rproc 23/23, nothing skipped (the UML skip needs SMP, which the board has). UML 65 pass / 1 skip on the fold. As modules on the board (PREEMPT kernel): `modprobe` twice, 37/37 and 23/23 each time, clean `rmmod`
- [x] New KUnit cases for V3-S2, S3, S4, S5 fail on the v3 code and pass on v4 — 2026-11-07, `77398ba801c3`, UML: each fix reverted inside v4 (the v4 tests do not compile against v3). Baseline all pass; S2 (`-EBUSY` on full) 6 fail incl. `test_held_message_dropped_after_timeout`; S3 (no latched-status clear) `test_tx_irq_armed_after_every_send`; S4 (no shutdown flag) `test_tx_irq_send_during_shutdown_refused`; S5 (no `RPROC_RUNNING` check) `test_vq_work_notifies_every_vring`. The S3 lock-section race itself needs two CPUs: code review only
- [x] Board (Cubie A5E): rpmsg ping + stress, mailbox unbind/bind, rproc stop/start loop,
      under PREEMPT_RT and PREEMPT with PROVE_LOCKING + DEBUG_ATOMIC_SLEEP: no splat —
      PREEMPT_RT without lock debugging done 2026-11-07: full sweep all 3 profiles PASS (rpmsg
      ping 1000 and stress 100k/16 in flight on DDR and SRAM, FreeRTOS, UIO, msgbox unbind/bind,
      write IRQ on SPI 205 full -> pop -> 1 IRQ, dmesg clean); 100 stop/start cycles with a
      50-ping run each, 0 failures, dmesg clean. TX IRQ column: 0 for the bare-metal ping
      firmware because it polls and sets `VRING_USED_F_NO_NOTIFY` (no kicks), 2000 for 2000
      FreeRTOS messages (one write IRQ per kick). PREEMPT_RT with PROVE_LOCKING +
      DEBUG_ATOMIC_SLEEP + gcov + FAILSLAB (`c22242e478f5`) 2026-11-07: full sweep all PASS,
      `debug_locks: 1` at the end; fail-nth over every allocation of rproc start/stop/probe and
      msgbox probe, 0 recovery failures, no splat from our drivers (the only WARNs are the core
      `rproc_add()` use-after-free, TODO CORE-RPROC-ADD). The first boot found a KUnit fixture
      bug that switched lockdep off (V4-LOCKDEP-KTEST, fixed). Coverage merged UML + board:
      100% line coverage of both drivers (V4-COV, `review/COVERAGE_20261107.md`). PREEMPT (non-RT) + PROVE_LOCKING + DEBUG_ATOMIC_SLEEP + DEBUG_OBJECTS, KUnit as modules: full sweep 42/42 PASS, 100 stop/start cycles 0 failures, `debug_locks: 1`, no reports
- [ ] Owner board-tests the final series

## 3. Review (the v3 lessons)

- [x] Concurrency pass through the subsystem core, not only the driver: for each callback,
      trace `drivers/mailbox/mailbox.c` / `drivers/remoteproc/remoteproc_core.c` for
      (a) a blocking send that times out, (b) unbind during traffic, (c) a second interrupt
      after `synchronize_irq()`, (d) stop/recovery while work or a threaded IRQ runs — 2026-11-07,
      [review/CONCURRENCY_PASS_20261107.md](review/CONCURRENCY_PASS_20261107.md): no High/Medium, two Low (C1, C2) for the owner
- [x] Every v1, v2 and v3 finding re-checked against the v4 code by re-running its failure
      scenario, not by confirming the code exists — 2026-11-07 /kreview + /kseries
      ([review/KREVIEW_KSERIES_20261107.md](review/KREVIEW_KSERIES_20261107.md)): all re-run and fixed
      except V3-K2a/b, reopened as K-1 and answered by the owner 2026-11-08 (option A, description fixed); late kernel test robot report on v3 7/7 ingested
      (V3-R1 = V3-S7, fixed). PM=n and built-in builds repeated on the fold tip: clean
- [x] Every precedent argument cites all peer drivers, including those that do it differently —
      2026-11-07: vq work (stm32, imx_dsp lock; imx, xlnx free the mailbox in stop), shared reset
      (sun6i-msgbox), `memory-region` (every remoteproc binding in next-20261105, K-1)
- [x] Every High/Medium finding from our reviews is fixed or answered by the owner (yes/no) — 2026-11-08: the only one, K-1 (Medium), answered: option A
- [x] Bindings: no `minItems` on a list unless the binding gives the hardware reason — owner exception (D10, 2026-11-08): the rproc `memory-region` keeps `minItems: 1` with the firmware reason and the valid layouts in the description (precedent `xlnx,zynqmp-r5fss`); the 3/7 reply restates it

## 4. Send (owner)

- [ ] Changelog in each patch answers every v3 comment; repeats say what was missed and that v4 fixes it ("Reported on v2 too; v3 did not fix it"), no "sorry" (owner 2026-11-08)
- [x] No first person (we/our/us) or "sorry" in code comments, commit messages, cover letter, replies (owner 2026-11-08) — 2026-11-08: 7.1 `22a93a2f6d9c` (5 msgbox comments: "the host"), 2/7 changelog line; fold tip `010b0ec0b69f`, `grep -i -wE "sorry|we|our|us"` on all 7 patches and both drafts: none; arm64 GCC W=1 drivers + KUnit as modules: 0 warnings; checkpatch 2/7: only the missing Signed-off-by. Cover letter: "I" lines reworded too (Tools section, remap note)
- [ ] Cover letter `TBD`s filled in; v3 link added
- [ ] Owner Signed-off-by on every patch; no AI trailers
- [ ] `git format-patch -v4 --cover-letter --base=<next tag>`
- [ ] Recipients as v3, except Chen-Yu at `wens@kernel.org` (MAINTAINERS; v3 went to `wens@csie.org`, TC-5), plus Cc Uwe Kleine-König `<u.kleine-koenig@baylibre.com>` (reviewed v3 2/7, 4/7; L-8);
      re-run `get_maintainer.pl` on the v4 patches
- [ ] New thread, no `--in-reply-to`; owner's go-ahead
- [ ] After sending: post [REPLIES_DRAFT.md](REPLIES_DRAFT.md) replies; record the lore link in
      `../SUBMISSION_TRACKER.md` and `../LORE_ARCHIVE_AND_SUBMISSION_INDEX.md`
