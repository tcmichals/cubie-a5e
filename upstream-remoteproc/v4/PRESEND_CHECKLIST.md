# v4 Pre-Send Checklist

Tick each line with the date, commit and result. A box without evidence is not done.

## 1. Build (on the linux-next base, each patch on its own)

- [ ] `W=1`, GCC and clang, built in and as modules
- [ ] `C=1` sparse clean on the new files (missed in v3: V3-S8)
- [ ] `CONFIG_PM=n` build, drivers and KUnit (missed in v3: V3-S7)
- [ ] `CONFIG_COMPILE_TEST` on a non-arm64 arch
- [ ] `make dt_binding_check DT_SCHEMA_FILES=...` for both bindings, and `dtbs_check`
- [ ] `checkpatch.pl --strict` on every patch

## 2. Tests

- [ ] KUnit: both suites built in and as modules, and under UML (`kunit.py run`)
- [ ] New KUnit cases for V3-S2, S3, S4, S5 fail on the v3 code and pass on v4
- [ ] Board (Cubie A5E): rpmsg ping + stress, mailbox unbind/bind, rproc stop/start loop,
      under PREEMPT_RT and PREEMPT with PROVE_LOCKING + DEBUG_ATOMIC_SLEEP: no splat
- [ ] Owner board-tests the final series

## 3. Review (the v3 lessons)

- [ ] Concurrency pass through the subsystem core, not only the driver: for each callback,
      trace `drivers/mailbox/mailbox.c` / `drivers/remoteproc/remoteproc_core.c` for
      (a) a blocking send that times out, (b) unbind during traffic, (c) a second interrupt
      after `synchronize_irq()`, (d) stop/recovery while work or a threaded IRQ runs
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
