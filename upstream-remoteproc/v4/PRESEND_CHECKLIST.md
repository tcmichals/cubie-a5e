# v4 Pre-Send Checklist

Tick each line with the date, commit and result. A box without evidence is not done.

## 1. Build (on the linux-next base, each patch on its own)

- [x] `W=1`, GCC and clang, built in and as modules — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): GCC 15.1 (Buildroot) and clang 21.1.8, `y` and `m` (both KUnit suites as modules, modpost run), 0 driver warnings. allnoconfig + only the drivers' dependencies
- [ ] `C=1` sparse clean on the new files (missed in v3: V3-S8). Check the log has `CHECK` lines: an
      old sparse (Ubuntu 0.6.4) is rejected by `scripts/checker-valid.sh` and `C=` is silently ignored
- [x] `CONFIG_PM=n` build, drivers and KUnit (missed in v3: V3-S7) — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): GCC W=1, 0 warnings; msgbox KUnit builds, rproc KUnit is off (it `depends on PM`)
- [x] `CONFIG_COMPILE_TEST` on a non-arm64 arch — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold): 32-bit ARM, clang W=1, `ARCH_SUNXI` off, drivers and KUnit as modules, 0 warnings. x86_64 not run: objtool needs `libelf-dev` on this host
- [ ] `make dt_binding_check DT_SCHEMA_FILES=...` for both bindings, and `dtbs_check` — `dt_binding_check` clean for both, 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold); `dtbs_check` not run yet
- [ ] `checkpatch.pl --strict` on every patch — 2026-11-07, `77398ba801c3` on the 7.1 base (repeat on the linux-next fold), on the unfolded commits: only "Missing Signed-off-by" (owner adds it), plus 2 over-100-column lines in `70509dc4e6fe` and a long changelog line in `4931baf9d66b`, both gone once the fixups are folded. Re-run on the folded series

## 2. Tests

- [ ] KUnit: both suites built in and as modules, and under UML (`kunit.py run`)
- [x] New KUnit cases for V3-S2, S3, S4, S5 fail on the v3 code and pass on v4 — 2026-11-07, `77398ba801c3`, UML: each fix reverted inside v4 (the v4 tests do not compile against v3). Baseline all pass; S2 (`-EBUSY` on full) 6 fail incl. `test_held_message_dropped_after_timeout`; S3 (no latched-status clear) `test_tx_irq_armed_after_every_send`; S4 (no shutdown flag) `test_tx_irq_send_during_shutdown_refused`; S5 (no `RPROC_RUNNING` check) `test_vq_work_notifies_every_vring`. The S3 lock-section race itself needs two CPUs: code review only
- [ ] Board (Cubie A5E): rpmsg ping + stress, mailbox unbind/bind, rproc stop/start loop,
      under PREEMPT_RT and PREEMPT with PROVE_LOCKING + DEBUG_ATOMIC_SLEEP: no splat
- [ ] Owner board-tests the final series

## 3. Review (the v3 lessons)

- [x] Concurrency pass through the subsystem core, not only the driver: for each callback,
      trace `drivers/mailbox/mailbox.c` / `drivers/remoteproc/remoteproc_core.c` for
      (a) a blocking send that times out, (b) unbind during traffic, (c) a second interrupt
      after `synchronize_irq()`, (d) stop/recovery while work or a threaded IRQ runs — 2026-11-07,
      [review/CONCURRENCY_PASS_20261107.md](review/CONCURRENCY_PASS_20261107.md): no High/Medium, two Low (C1, C2) for the owner
- [ ] Every v1, v2 and v3 finding re-checked against the v4 code by re-running its failure
      scenario, not by confirming the code exists
- [ ] Every precedent argument cites all peer drivers, including those that do it differently
- [ ] Every High/Medium finding from our reviews is fixed or answered by the owner (yes/no)
- [ ] Bindings: no `minItems` on a list unless the binding gives the hardware reason

## 4. Send (owner)

- [ ] Changelog in each patch answers every v3 comment; repeats say "fixed, sorry I missed it in v3"
- [ ] Cover letter `TBD`s filled in; v3 link added
- [ ] Owner Signed-off-by on every patch; no AI trailers
- [ ] `git format-patch -v4 --cover-letter --base=<next tag>`
- [ ] Recipients as v3, except Chen-Yu at `wens@kernel.org` (MAINTAINERS; v3 went to `wens@csie.org`, TC-5);
      re-run `get_maintainer.pl` on the v4 patches
- [ ] New thread, no `--in-reply-to`; owner's go-ahead
- [ ] After sending: post [REPLIES_DRAFT.md](REPLIES_DRAFT.md) replies; record the lore link in
      `../SUBMISSION_TRACKER.md` and `../LORE_ARCHIVE_AND_SUBMISSION_INDEX.md`
