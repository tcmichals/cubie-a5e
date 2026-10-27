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

### Vring pool in DRAM or SRAM (owner, 2026-10-27)
`memory-region[0]` is a `shared-dma-pool` and may sit in DRAM or on an E906 SRAM; both are
board-tested every sweep (profile 1/1T: `cubie-a5e-flight-stack`, DRAM `0x48000000`; profile 2:
`cubie-a5e-testPingRpmsgSram`, SRAM space 1 `0x072c0000`). A pool on an SRAM reuses that SRAM's
mapping. The final review expected DT maintainers to object to SRAM in `/reserved-memory`
(mainline also uses `mmio-sram` + a `sram` phandle, TI K3, meson-mx-ao-arc): keep both, say so in
the cover letter, and offer the `sram` phandle form only if a maintainer asks. The series DTS
itself has no `memory-region`.

### SRAM reg-names follow the manual (owner, 2026-10-27)
The rproc `reg-names` for the two SRAMs are `sram-a3-0` and `sram-a3-1`: the T527 manual (V0.92,
memory map p.32, MCU_SRAM) calls them SRAMA3_0 (`0x07280000`) and SRAMA3_1 (`0x072C0000`), and
mainline sunxi DT lower-cases manual names (`sram-c`, `sram-a3-a4`). `r_sram`/`r_sram1` were
dropped: `r_` means the PRCM domain on sunxi. Kernel `eea917a5d5fb`.

### msgbox carries Allwinner's copyright (2026-10-27)
The register definitions came verbatim from the vendor `bsp/drivers/msgbox/sunxi-msgbox.c`
(Copyright 2020-2023 Allwinner, GPL-2.0-or-later; `A7A_kernel/linux-a733`), so the driver keeps
"Based on" and carries Allwinner's copyright line, as `sunxi_rproc.c` does.

### Remap register `0x07010364` is shared (D9)
The remoteproc node maps the 4-byte `remap` register and read-modify-writes bit 1 only
(SRAM A3_2 → MCU); bit 0 belongs to the DSP. It works on silicon. The cover letter explains
it. Move it to a syscon or the R-CCU only if maintainers ask.
2026-10-27 (owner, after the harsh review): kept for v3. The cover letter also offers the sunxi
SRAM driver (`allwinner,sram = <&...>`) for SRAM A3_2, which a reviewer is likely to ask for.
**2026-10-27, under question (REMAP-1):** the T527 manual says bit 1 shares **SRAMA3_2** (512 KB at
`0x07300000`, listed under NPU) with MCU_SYS, and our SRAM space 1 at `0x072C0000` is SRAMA3_1.
The vendor BSP (`A7A_kernel/linux-a733/bsp`) never sets it for the E906: `0x07010364` is a `reg`
of the HiFi4 node only, and only `sunxi_rproc_hifi4_boot.c` writes it (bit 0). "Works on silicon"
was only ever measured with the bit set. Board check tonight with the bit left alone; if the
E906 still uses space 1, the owner decides whether the driver drops the remap write and the
`remap` reg (which removes this shared-register question from the series).

### Message box: all four banks clocked and out of reset (D12, redone)
The original D12 (host bank only) was wrong and was redone before v3: the binding and DTS list
all four clocks and resets, and probe refuses a DT with fewer (2026-10-27). Each bank has its own bus clock
and reset; with only the ARM bank's described, the RV bank stays gated and in reset, so Linux's
doorbells to the E906 never arrived and `last_tx_done()` read a gated bank. Measured:
[ALLWINNER_SUN55I_MSGBOX_REFERENCE.md](../docs/platforms/ALLWINNER_SUN55I_MSGBOX_REFERENCE.md) §6.
Original text kept below for the record.

**Unbind (owner, 2026-10-27):** all four banks stay clocked and out of reset, but the shared
reset is only deasserted at probe and never asserted, on unbind or on a probe failure, as in
`sun6i-msgbox`: the other banks carry traffic between the other processors. The bus clocks
are still released on unbind (devm), as `sun6i-msgbox` disables its clock in `remove()`.

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
2026-10-27 (owner): kept in the binding, whose description now says the A523 has no dedicated
line for it and it is present only where the system integration provides one.

### Compatible stays `allwinner,sun55i-a523-rproc` (owner, 2026-10-27)
The harsh review suggested `…-e906`, since the SoC also has a HiFi4 DSP. Kept for v3; rename
in a later version only if a maintainer asks.

### No system suspend support (owner, 2026-10-27)
The E906 is a co-processor next to a Linux that runs all the time; this is not a suspend/wake
product, and suspend would need ARM Trusted Firmware support the board does not have. The
remoteproc driver has no `dev_pm_ops`, and no platform `.shutdown` for kexec (`prepare()` holds the
core in reset when the next kernel boots it). The cover letter says suspend is not supported. The
framework comparison (`peer-reviews/RPROC_FRAMEWORK_FEATURE_MATRIX_20261027.md`, N2/R3) raised both;
do not re-raise them.

### Mailbox binding requires all four interrupts (owner, 2026-10-27)
The four msgbox lines (host bank read, DSP/CPUS/RISC-V bank write) exist on every A523, so the
binding lists all four, with `interrupt-names`, as required; no `minItems`, no
`allwinner,tx-done-irq` conditional. This answers Krzysztof's v1 question "How could an
interrupt be optional?". `allwinner,tx-done-irq` only selects how the driver learns that a send
is done (1 ms poll or the remote bank's write interrupt); received messages use the host bank
read interrupt in both modes, and the driver requests the write lines only in tx-done-irq mode.

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

- **Common to both modes:** a kick is done once the receiver's FIFO accepted it and has room for
  the next one. A kick is a doorbell; the data is in the vrings (Case Study 13, lesson 5).
  `send_data()` never writes a full FIFO (it returns `-EBUSY`; a write to a full FIFO corrupts the
  count, reference §6a).
- **Timer (property absent, default; e.g. servers):** controller `txdone_poll`; `last_tx_done()`
  returns `true` while `count < 8`, and a full FIFO is re-checked every `txpoll_period` (1 ms).
  No TX interrupts.
- **ISR (`allwinner,tx-done-irq`; e.g. the flight controller), revised 2026-10-17 to the OMAP
  model:** controller `txdone_irq`, no polling. `send_data()` arms the write interrupt (threshold
  field 0 = one free slot) **after every send**, clearing its latched status first. The interrupt
  is level (status = free slots ≥ threshold, §6a), so it fires at once if the FIFO still has room,
  or when the receiver reads a message; the handler disables and acks it, then calls
  `mbox_chan_txdone()`, and the core writes the next queued kick from there. One TX interrupt per
  kick, no timer in the path. This is the transmit flow in the manuals (T527 V0.94 §2.12.4.1,
  A733 V1.00 §6.1.5.1, Figure 6-2) and what `omap-mailbox` does (`omap_mbox_chan_send()` enables
  the "not full" TX interrupt after every send). The E906 bank's write interrupt reaches the A55
  on **SPI 205** (manual Table 2-15, measured; the vendor DT's SPI 174 is the CPUS bank). The line
  is requested only while a channel to that bank is started.
- **Why not "only when full" (the first ISR version, 2026-10-16):** the mailbox core sends one
  message at a time and needs a completion for each. With the interrupt armed only when full,
  completion in the normal case still came from the poll timer, a thread wake-up per kick on
  PREEMPT_RT, and a 16-in-flight `stress_rpmsg` overflowed the 20-slot core queue (46 times; timer
  mode ≥ 1,598, 2026-10-17). Ethernet-style "stop at the high-water mark" batching does not map
  onto the mailbox core without another per-kick completion path (client `knows_txdone`, rejected
  in v2, or deferred work, also a thread wake-up on RT).
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
