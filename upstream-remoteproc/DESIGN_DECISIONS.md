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

### Message box: host bank clock and reset only (D12)
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

### Crash interrupt kept (D14)
The driver keeps the optional crash IRQ. No DTS wires it and the vendor BSP has none, so the
cover letter must not claim it is tested.

### KUnit stays in the series (D15)
Both KUnit patches stay, but every test must exercise driver behaviour (see
[UPSTREAM_LESSONS_LEARNED.md](UPSTREAM_LESSONS_LEARNED.md) §4). In the series they go last,
and the cover letter says they can be dropped or taken separately without affecting the
drivers.

### Mailbox TX pacing
`last_tx_done()` reports "remote drained" (`count == 0`) and the remoteproc client uses
`knows_txdone = false`. See Lessons 2.1 and 3.2. Throughput work (P1) comes after v3.

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
