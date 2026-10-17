# Design Decisions — sun55i Remoteproc & Message Box

Decisions the project owner has made for the Allwinner A523/A527/T527 remoteproc and
message box drivers and the v3+ upstream series. Each one was argued once already.

A review comment (from a maintainer, a bot, or an AI) that disagrees with a decision does not
change it on its own: bring it to the owner, then update this file, the code and
[TODO.md](../TODO.md) §5.6a together. A maintainer asking for the change on the list is a
good reason to revisit; a vendor BSP or another driver doing it differently is not.

Reviewer-driven rules (what maintainers told us and why) live in
[UPSTREAM_LESSONS_LEARNED.md](UPSTREAM_LESSONS_LEARNED.md). Fix status lives in
[TODO.md](../TODO.md) §5.6a. IDs (D7, D9, ...) refer to that tracker.

---

## Driver and binding

### A733 / E902 placeholders stay
The per-SoC structure in `drivers/remoteproc/sunxi_rproc.c` (`struct sunxi_rproc_cfg`, the
ATT table, `.data` in the of_match table) and the A733 / XuanTie E902 columns in the docs
are kept on purpose, even though v3 only carries the A523 (E906) compatible. A733 support is
planned. Correct E906/T527 facts, but do not remove the A733 rows or call the indirection
dead code.

### DRAM firmware stays (D7)
Every `memory-region` (`vram`, `dram`, `trace`) is registered as a carveout so firmware can
load into DRAM. `da_to_va()` handles only the SRAM windows; the remoteproc core resolves
DRAM, trace and vrings from the carveouts. DRAM support is not dropped to satisfy a review
comment.

### Memory regions are a listed, positional set (D10, 2026-10-14)
The binding lists the `memory-region` items in order: `vram` (the DMA pool, index 0), then
`dram`, then `trace`, with matching `memory-region-names`. Krzysztof asked for listed items
in v1 (KK8).

This is the layout TI K3, MediaTek SCP and OMAP use: index 0 is the pool, and the core binds
it to the virtio device when there is no `vdev0buffer` carveout
(`drivers/remoteproc/remoteproc_virtio.c`). We do not switch to the stm32 / i.MX / ZynqMP
style (a free `memory-region` list whose meaning comes from reserved-memory node names such
as `vdev0buffer`, `vdev0vring0`), even though the vendor BSP uses it:
- it loosens the binding Krzysztof asked us to tighten;
- a `vdev0buffer` carveout needs `rproc_of_resm_mem_entry_init()`; registered like our other
  carveouts, the core computes the buffer's physical address from our remapped pointer, which
  is wrong for SRAM;
- our firmware uses `FW_RSC_ADDR_ANY` and lets Linux place the vrings, which is the pool case.

### Remap register `0x07010364` is shared (D9)
The remoteproc node maps the 4-byte `remap` register and read-modify-writes bit 1 only
(SRAM A3_2 → MCU); bit 0 belongs to the DSP. It works on silicon. The cover letter explains
it. Move it to a syscon or the R-CCU only if maintainers ask.

### Message box: host bank clock and reset only (D12) — REOPENED 2026-10-16
**This decision was wrong and must be redone before v3 is sent.** Each bank has its own bus clock
and reset; with only the ARM bank's described, the RV bank stays gated and in reset, so Linux's
doorbells to the E906 never arrived and `last_tx_done()` read a gated bank. Measured:
[ALLWINNER_SUN55I_MSGBOX_REFERENCE.md](../docs/platforms/ALLWINNER_SUN55I_MSGBOX_REFERENCE.md) §6.
Original text kept below for the record.

#### Original D12 text
The msgbox node enables only the ARM host bank's clock and reset, like the vendor DT and the
board-tested 7.1 kernel. The other banks belong to the other processors; asserting their
resets on unbind would break them.

### One mailbox channel (D11, 2026-10-14)
The remoteproc node uses a single message box channel: `mboxes = <&msgbox 9>`, no
`mbox-names`. The message box has slots per processor pair (ARM ↔ E906, ARM ↔ DSP, ...), and
each channel has a FIFO in each direction, so one channel carries both the host's vring kicks
and the E906's notifications. The firmware already uses it that way (`hal::Rpmsg` sends on
FIFO 1, which is Linux channel 9). The earlier `"rx"` channel 8 never carried rpmsg traffic;
it only appeared to work because both channels shared one client. This matches the vendor
BSP, which also uses one channel.

### Mailbox is optional; UIO mode uses its own base device tree (2026-10-14)
Without an `mboxes` property the driver starts in standalone mode (load, start, stop only).
With `mboxes` present it waits for that mailbox. The driver does not look at the message box
node to guess whether the mailbox went to userspace (the old `generic-uio` check was removed
in `b6d558c36fbd`). Profile 3 (`cubie-a5e-testPing`, mailbox owned by userspace through the project-only
`sun55i-msgbox-uio` driver, which also keeps its clock and reset on) boots
`sun55i-a527-cubie-a5e-uio.dtb`, a project-only board DTS that deletes the remoteproc
`mboxes`, selected with `base_dtb=` in `config.txt`. A DT overlay cannot delete a base-tree
property, so this cannot be done in `cubie-a5e-testPing.dtso`.

Why it is done this way, so nobody tries the single overlay again: `/delete-property/` inside an
overlay is silently dropped by `dtc` (the overlay format can only add or set). The old
`cubie-a5e-uio.dtso` relied on it, so the remoteproc kept waiting for a mailbox that never came
(no `remoteproc0`); it also used `generic-uio`, which leaves the message box unclocked so the
doorbell interrupt can never fire, and mapped the wrong SRAM page (`0x07131000`, the ping slot is
at `0x072B0000`). It was removed on 2026-10-16. The working split: the `-uio` base DTB removes
`mboxes` (remoteproc only loads, starts and stops the E906), and the `cubie-a5e-testPing` overlay
binds the message box to the project-only `sun55i-msgbox-uio` driver (clock and reset on, registers
and the SRAM ping slot as `/dev/uio0`).

### Crash interrupt kept (D14)
The driver keeps the optional crash IRQ. No DTS wires it and the vendor BSP has none, so the
cover letter must not claim it is tested.

### KUnit stays in the series (D15)
Both KUnit patches stay, but every test must exercise driver behaviour (see
[UPSTREAM_LESSONS_LEARNED.md](UPSTREAM_LESSONS_LEARNED.md) §4). In the series they go last,
and the cover letter says they can be dropped or taken separately without affecting the
drivers.

### E906 receive: polling by default, interrupt mode available (owner, 2026-10-16)
The E906 polls its message box (interrupt off): a dedicated core, lowest latency, deterministic.
The firmware also supports an interrupt mode (wake on the message box interrupt) and both modes
are tested.

### Message box TX completion: the device tree selects timer or ISR (owner, 2026-10-16)
Replaces "Mailbox TX pacing" (`count == 0`, poll) and the open "poll or write interrupt" item.
The system builder chooses, per board, in the device tree:

- **Common to both modes:** `last_tx_done()` returns `true` while the receiver's FIFO has room
  (`count < 8`), so a kick goes out at once whenever there is space. A kick is a doorbell; the
  data is in the vrings (Case Study 13, lesson 5), so "accepted by the FIFO" is done.
- **Timer (property absent, default; e.g. servers):** when the FIFO is full the mailbox core
  re-checks every `txpoll_period` (1 ms). No TX interrupts.
- **ISR (`allwinner,tx-done-irq`; e.g. the flight controller):** the write interrupt
  (threshold field 0 = one free slot) is enabled **only when the FIFO is full** (count 8) and
  disabled in the handler when it fires (count 8 → 7, the receiver read one). The handler then
  lets the next kick go at once. No timer wait in a burst, so low latency and low jitter. The
  write interrupt is level (status = free slots ≥ threshold, measured §6a), which is why it must
  stay off whenever the FIFO is not full. It is raised on the receiver's bank line (SPI 174 for
  the E906 bank).
- **Precedent for a DT property that selects the TX mode:** `ti,mbox-send-noirq` in
  `Documentation/devicetree/bindings/mailbox/ti,omap-mailbox.yaml` (read with
  `of_property_read_bool()` in `omap-mailbox.c`). OMAP also uses a "FIFO not full" TX interrupt.
- The remoteproc client is unchanged: `knows_txdone = false`, `tx_block = false`.

Why: with `count == 0` and the 1 ms poll, a burst left every kick waiting for the timer:
`ping_rpmsg -n 10000 -D 0` went from ~200 µs to ~990 µs RTT and the 20-slot core queue
overflowed (`MBOX_TX_QUEUE_LEN`, kick `-ENOBUFS`) once the RV bank was clocked (D12). Facts:
[ALLWINNER_SUN55I_MSGBOX_REFERENCE.md](../docs/platforms/ALLWINNER_SUN55I_MSGBOX_REFERENCE.md) §5, §6a.

### E906 D-cache is off (C1)
The E906 runs with `mhcr.DE = 0`. Linux maps the DMA pool and carveouts uncached
(`shared-dma-pool`, `MEMREMAP_WC`, no `dma-coherent`), so no cache maintenance is needed
today. Enabling the cache and the clean/invalidate helpers belong to the AbstractX firmware,
after v3.

---

## Workflow

### Commit and push when work is done
Work happens on more than one PC (`tools/sync_kernel.sh pull`). After committing, push
`linux-cubie` (`cubie-linux-7.1`) and `cubie-a5e` (`main`). Upstream submission branches are
never pushed, and patches are never sent to a mailing list, without the owner's explicit
go-ahead.

### Finish a batch of fixes before building
Do not start a board kernel build in the middle of a batch. Compile-check the touched drivers
while fixing, then build and test once when the batch is complete.

### Develop in `linux-cubie`, never in the build output
`bld.a5e/` and `bld.a7a/` are Buildroot output: they are for builds, not development. Never
edit, copy files into, or run `make` in `bld.*/build/linux-custom` (Buildroot syncs it from
`linux-cubie` and may delete it). Compile-check from `linux-cubie` with a separate output
directory and the board config, for example:

```sh
make O=/tmp/kobj ARCH=arm64 CROSS_COMPILE=aarch64-linux- olddefconfig   # .config from project-cubie-a5e/board/radxa/cubie_a5e/linux.config
make O=/tmp/kobj ARCH=arm64 CROSS_COMPILE=aarch64-linux- W=1 drivers/remoteproc/sunxi_rproc.o
```

The Buildroot cross compiler in `bld.a5e/host/bin` may be used on `PATH`.

### Fixes land on 7.1 first
Every fix lands on `cubie-linux-7.1` and is board-tested there, then is folded into the
matching patch on `next-sun55i-rproc-msgbox`, which the series is exported from.

### Building a new revision of the series (2026-10-15)
v2 and the first v3 drafts all sat on next-20260921 and the local and GitHub copies of the
series branch had drifted apart. For every revision from v3 on:

1. **One series branch.** `next-sun55i-rproc-msgbox` in `linux-cubie` is the only series branch;
   the GitHub copy matches it. Keep the previous revision's tip under a name
   (`next-sun55i-rproc-msgbox-v3draft`, `origin/v2-sun55i-rproc-msgbox`) for `git range-diff`.
2. **Fold the tested code.** The driver, test and binding files in the series are byte-identical to
   `cubie-linux-7.1` (check with `git diff <series> cubie-linux-7.1 -- <files>`). In shared files
   (Kconfig, Makefile, MAINTAINERS, the SoC dtsi) only our entries and nodes are taken over.
3. **Current base.** Rebase onto the latest linux-next tag (`git fetch --no-tags linux-next tag
   next-YYYYMMDD`, remote `https://git.kernel.org/pub/scm/linux/kernel/git/next/linux-next.git`)
   right before export.
4. **Check on that base:** W=1 build of both drivers and KUnit suites, `dt_binding_check` for both
   bindings, `dtbs_check` for the sun55i boards, `checkpatch.pl --strict` on the exported patches.
5. **Export with the base recorded:** `git format-patch -v<N> --cover-letter --base=<next tag>`.
6. **Send** only with the owner's go-ahead, to the list in TODO.md §5.6a T3, with
   `--suppress-cc=all` and no `--in-reply-to`; `--dry-run` first.
