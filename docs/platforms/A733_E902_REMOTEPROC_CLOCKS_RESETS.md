# A733 E902 remoteproc: clocks, resets, registers, message box

Reference for everything Linux touches to run the XuanTie E902 on the A733 (Cubie A7A).
Sources: A733 User Manual (memory map, CCU chapters), the vendor BL31 disassembly
([rebuild guide §6.1](CUBIE_A7A_E902_REBUILD_GUIDE.md)), `linux-cubie` branch
`a7a_remoteproc_camera_usb` (`ccu-sun60i-a733-r.c`, `ccu-sun60i-a733.c`,
`sun60i-a733-cubie-a7a.dts`), the overlay
[`cubie-a7a-rproc.dtso`](../../project-cubie-a5e/dts-overlay/allwinner/cubie-a7a-rproc.dtso), and silicon
readbacks on the board (2026-11-10/11). Status: all of this was board-tested on 2026-11-11 with the
**stock vendor BL31** and no SCP. RPMsg ping: 1000 round trips, average 120.5 µs.

## 1. Clocks

| DT name | Provider / id | Register (bit) | What it is | Needed |
| :--- | :--- | :--- | :--- | :--- |
| `core` | `r_ccu` `CLK_RISCV` (27) | R-CCU `0x0701021c` bit 0 | E902 core clock gate | yes |
| `bus` | `r_ccu` `CLK_RISCV_CFG` (26) | R-CCU `0x0701021c` bit 1 | E902_CFG block bus gate | yes |
| — | `r_ccu` `CLK_RISCV_24M` (25) | R-CCU `0x07010210` bit 31 (mux + gate) | 24 MHz clock the vendor BL31 enables | **no**: the E902 and its timer run with it off (§6.2) |
| — | `r_ccu` `CLK_R_CPUCFG` | R-CCU `0x0701022c` bit 0 | R_CPUCFG bus gate | `CLK_IS_CRITICAL`, always on |
| msgbox | `ccu` `CLK_MSGBOX0` (35) | CCU `0x02001744` bit 0 | host message box bank gate (`bus-msgbox0`) | yes |
| msgbox | `r_ccu` `CLK_R_MBOX` (13) | R-CCU `0x0701017c` bit 0 (`S_MBOX_BGR_REG`) | E902-side bank (`S_MBOX` `0x07094000`) gate | **yes**: off by default; without it the E902 bank does not work |

Note: the main CCU is at `0x02001000`, so `bus-msgbox0` is `0x02001744`. The R-CCU (R_PRCM) is at `0x07010000`.

## 2. Resets

| DT name | Provider / id | Register (bit) | Polarity | What it is |
| :--- | :--- | :--- | :--- | :--- |
| `cfg` | `r_ccu` `RST_BUS_RISCV_CFG` (12) | R-CCU `0x0701021c` bit 16 | 1 = released | E902_CFG block. **While held, the block ignores writes**, including the start address |
| `core` | `e902_core_rst` 0 (overlay node, `allwinner,sun6i-a31-clock-reset` = mainline `reset-simple`) | R_CPUCFG `0x07050008` bit 0 | 1 = core runs | E902 core run/stop. Clearing it stops the core |
| msgbox | `ccu` `RST_BUS_MSGBOX0` (7) | CCU `0x02001744` bit 16 | 1 = released | host bank |
| msgbox | `r_ccu` `RST_BUS_R_MBOX` (3) | R-CCU `0x0701017c` bit 16 | 1 = released | E902 bank |

## 3. Other registers

| Register | Address | Use |
| :--- | :--- | :--- |
| E902_CFG | `0x07032000` (DT `reg` "cfg", 0x400) | E902 configuration block |
| `E902_STA_ADD_REG` | `0x07032204` | Start address (E902 view). Power-on value `0x40014000`. Write only **after** `cfg` is released. Behind R_SPC port 3, but readable and writable from Linux with the stock BL31 (§6) |
| SRAM A2, general part | host `0x00044000`, 0x30000 (DT `reg` "sram") | Firmware (E902 `0x40004000`) |
| `0x50005120` bit 0 | — | The vendor BL31 clears it; not identified; not needed |

## 4. Start and stop sequence (vendor BL31 order, implemented by `sunxi_rproc.c`)

1. `prepare()`: enable `core` + `bus` clocks, release `cfg` (R-CCU `0x0701021c` = `0x00010003`).
2. Load the ELF into SRAM A2 at host `0x44000` (and DRAM carveouts).
3. `start()`: write the entry to `0x07032204` (for example `0x40004000`), then release `core` (`0x07050008` bit 0 = 1).
4. `stop()`: assert `core` (`0x07050008` bit 0 = 0). The core stops at once.

Wrong order (address written while `cfg` is held): the write is lost and the E902 starts at `0x40014000`.

## 5. Memory map as the E902 sees it (A733 User Manual)

| E902 address | Host address | Size | Use |
| :--- | :--- | :--- | :--- |
| `0x40000000`–`0x40003FFF` | `0x00040000`–`0x00043FFF` | 16 KB | CPUS-only part of SRAM A2: **the E902 does not run code here** |
| `0x40004000`–`0x40033FFF` | `0x00044000`–`0x00073FFF` | 192 KB | Firmware (`e902_sram.ld`), driver ATT |
| `0x40034000`–`0x7FFFFFFF` | same | DRAM | Carveouts, vrings (`0x4e000000`, 16 MB, `shared-dma-pool`) |
| `0x80000000`–`0xBFFFFFFF` | — | 1 GB | DRAM window via `e902_remap` (not used) |
| peripherals | same | — | msgbox banks `0x03004000` (host), `0x07094000` (E902) |

## 6. Message box

- Two banks: host `0x03004000` (GIC SPI 0, Linux IRQ shows as GIC 32) and E902 `S_MBOX` `0x07094000` (SPI 211). Vendor layout `sun60iw2`. Driver: `sun55i-msgbox` A733 variant (`allwinner,sun60i-a733-msgbox`); the old `sun6i-a31-msgbox` fallback must not be in the DT (it binds first and leaves channel directions unset → WARN and RPMsg hang).
- Each side writes into the **other** bank's FIFO. The firmware (`firmware/common/hal/msgbox.cpp`, `CORE_E902`) rings host channel 1 (`0x03004000 + 0x70 + 1·4`); the host must listen on channel 1: overlay `mboxes = <&msgbox 1>` (A5E: `<&msgbox 9>`).
- Debug: host `IRQ_EN` `0x03004020`, `IRQ_STA` `0x03004024` (bit 2·n = channel n RX), `MSG_STA(n)` `0x03004060 + 4n`. A message waiting in `MSG_STA(1)` with `IRQ_EN` = `0x1` means the DT uses the wrong channel (seen 2026-11-11).

## 7. Boot firmware (TF-A)

- Stock vendor BL31, no `scp` in the FIT (`patch_fit_no_scp.py`). The BL31 port-3 binary patch is **not** needed (tested 2026-11-10); BL31-from-source work continues for other reasons (TODO).
- Without the SCP, nothing else starts the E902: the U-Boot 2026.04 blob never starts the arisc (debug log Milestone 21).
