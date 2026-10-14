# Project Decisions (read before changing driver code, docs, or reviews)

Decisions the project owner has made. A review (AI or human) that disagrees does not change
them by itself: raise it with the owner, then update this file and the code together.
Hardware facts and the v3 fix tracker live in `TODO.md` §5.6 / §5.6a.

## Code and design

1. **A733 / E902 placeholders are deliberate.** The per-SoC structure in
   `drivers/remoteproc/sunxi_rproc.c` (`struct sunxi_rproc_cfg`, the ATT table, `.data` in the
   of_match table) and the A733 / XuanTie E902 columns in the docs stay, even though v3 only
   carries the A523 (E906) compatible. Do not remove them or flag them as dead code. Correct
   E906/T527 facts, but leave the A733 rows in place.
2. **DRAM stays (D7).** Every `memory-region` (`vram`, `dram`, `trace`) is registered as a
   carveout so firmware can load into DRAM. `da_to_va()` handles only the SRAM windows; the
   remoteproc core resolves DRAM, trace and vrings from the carveouts. Never drop DRAM support
   to satisfy a review comment.
3. **Remap register `0x07010364` is shared (D9).** The remoteproc node maps the 4-byte `remap`
   reg and read-modify-writes bit 1 only (bit 0 belongs to the DSP). It works on silicon. The
   cover letter explains it; move it to a syscon/r-ccu only if maintainers ask.
4. **KUnit stays in the upstream series (D15)**, but every test must exercise driver behaviour.
   No tests that compare macros with the same literals.
5. **E906 D-cache is off** (`mhcr.DE = 0`). Linux maps the DMA pool and carveouts uncached
   (`shared-dma-pool`, `MEMREMAP_WC`; no `dma-coherent`), so no cache maintenance is needed
   today. Cache enable and clean/invalidate belong to AbstractX, not to this repo's tests.

## Workflow

6. **Commit and push when work is done.** The owner works on more than one PC and pulls with
   `tools/sync_kernel.sh pull`. Push `linux-cubie` (`cubie-linux-7.1`) and `cubie-a5e` (`main`)
   after committing. Never push upstream submission branches or send patches to mailing lists
   without explicit approval.
7. **Finish the fix set before building.** Do not start board kernel builds in the middle of a
   batch of fixes; compile-check the touched drivers (`make W=1 drivers/...`) instead, then build
   once when the batch is complete.
