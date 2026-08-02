# Radxa Logic Porting Guide (Shenmintao Target)

## Goal
We are using the clean architecture of the upstream `shenmintao` driver (`aic8800-driver-src`) but injecting the known-working hardware initialization logic from the `Radxa` driver for the AIC8800D80 chip on the Allwinner T527 (Cubie A5E).

## Current State & Milestones (Up to BUILD_148)
1. **Hardware Interrupts & IPC Verification**: V3 SDIO interrupts (`intstatus=0x01`), real-time RX packet processing (`enq_rxpkt len=512`), IPC memory read (`0x40500000 -> chip_id=0x07, chip_sub_id=0x02`), and NVRAM userconfig loading (`aic_userconfig_8800d80.txt`) are **100% VERIFIED WORKING ON HARDWARE**.
2. **Complete `PRODUCT_ID_AIC8800D80` Audit**: All MAC config requests (`rwnx_send_me_config_req`, `rwnx_send_me_chan_config_req`), RF calibrations (`aicwf_set_rf_config_8800d80`), 80MHz bandwidth checks, and Wi-Fi 6 MCS 10/11 maps have been updated across the driver.
3. **Interrupt Pin Routing Bug Fix (BUILD_148)**: Identified that writing `0x00000006` to register `0x40504084` rerouted chip interrupts to the OOB GPIO pin instead of SDIO DAT1. Removed this write to match Radxa (`CONFIG_OOB=n`), keeping interrupts on SDIO DAT1 for `cmd 123` (`MM_SET_STACK_START_REQ`) response.
4. **SDIO Wakeup Check Fix (BUILD_147)**: Corrected `aicwf_sdio_wakeup()` to read `wakeup_reg` (`0x01`) and check `(val & 0x1) == 0` per Radxa line 1359.
5. **Target Image**: `/home/tcmichals/projects/cubie/bld/images/sdcard.img`

## Decoded Radxa Hardware Logic (The Blueprint)
*This is the exact line-by-line hardware logic reverse-engineered from the Radxa driver that must be maintained in the `shenmintao` driver.*

### 1. V3 SDIO Register Map & Wakeup
- The AIC8800D80 uses a V3 register map. 
- **Wakeup Sequence**: Write `0x11` to `wakeup_reg` (`SDIOWIFI_INTR_TO_DEVICE_REG_V3` at offset `0x02`). Then poll `sleep_reg` (`0x01`); the chip is awake when bit 4 is set (`val & 0x10`).
- **Flow Control**: V3 flow control registers are at `0x03` (`SDIOWIFI_FLOW_CTRL_Q1_REG_V3`), not the legacy V1/V2 offsets.

### 2. In-Band Interrupt Enabling
- The SDIO Core Card Common Control Register (CCCR) must be enabled physically.
- **Action**: The driver MUST write `0x07` to SDIO CCCR register `0x04` (`sdio_f0_writeb(sdiodev->func, 0x07, 0x04, &ret)`). If this is missing, the Allwinner host will never see the chip's responses.

### 3. The 3-Step LMAC Boot Sequence
Unlike older chips, the AIC8800D80 ROM (`0x00010000`) contains the full LMAC stack. No firmware upload is required. The exact sequence in `rwnx_ic_system_init()` must be:
1. `rwnx_send_dbg_mem_read_req(rwnx_hw, 0x40500000, &rd_cfm);` -> to verify IPC and read `chip_id`.
2. `rwnx_plat_userconfig_load_8800d80();` -> parse NVRAM locally on the host.
3. `cmd 123` (`MM_SET_STACK_START_REQ`) -> sent directly to ROM.

### 4. RX Polling Loop
Because Allwinner drops the DAT1 In-Band interrupt edge, all control messages sent to the chip must be synchronously polled immediately after transmission:
```c
for (poll_cnt = 0; poll_cnt < 100; poll_cnt++) {
    sdio_claim_host(sdiodev->func);
    aicwf_sdio_hal_irqhandler(sdiodev->func);
    sdio_release_host(sdiodev->func);
    if (sdiodev->cmd_mgr.queue_sz == 0) break;
    msleep(2);
}
```

## World-Class Mainline Driver Mandates (The Standard)
*We are building a clean, upstream-ready driver. The AI must enforce these modern Linux kernel rules for all new code:*

1. **No Legacy Wireless Extensions (WEXT)**: The driver must be purely modern `cfg80211` / `nl80211`. Do not resurrect `iw_handler` spaghetti.
2. **No Custom Kernel Threads for IO**: Never spawn `kthread_run` for RX/TX polling. We strictly use native Linux `work_struct`s (`INIT_WORK` / `queue_work`) or threaded IRQs (`request_threaded_irq`).
3. **No SoftIRQ Deadlocks**: Never use `mdelay()` inside a `softirq` context or spinlock-guarded loops. Use `msleep()` or proper asynchronous timers if waiting is required.
4. **Strict MAC/PHY Decoupling**: The MAC layer data path (`rwnx_tx.c`, `aicwf_txrxif.c`) must NEVER contain hardware-specific conditionals like `#ifdef AICWF_SDIO_SUPPORT`. Always rely on the `bus_if->ops` HAL abstraction.

## Resources for the AI Assistant
- **Target Source Code**: `/home/tcmichals/projects/cubie/cubie-a5e/aic8800-driver-src/`
- **Known-Good Radxa Reference**: Extracted to `/tmp/radxa_ref/` (originally from `aic8800-radxa-working-backup.tar.gz`).
- **Target Image (For Flashing)**: `/home/tcmichals/projects/cubie/bld/images/sdcard.img`

## Actionable Line-by-Line Plan (Post-BUILD_130)
*Depending on the `dmesg` output from the physical board test, the AI must follow these exact steps:*

1. **If IPC read fails (`[aic8800] IPC read 0x40500000 FAILED`)**:
   - The chip ROM's read handler is dropping the IPC.
   - **Action**: Check `/tmp/radxa_ref/aic8800/aic8800_fdrv/rwnx_main.c` (specifically `rwnx_ic_system_init`) to see if Radxa writes `0x00000006` to `0x40504084` (the OOB enable register) *before* doing the IPC read. If yes, port that exact write into `system_config_8800d80()`.

2. **If commands timeout (`[aic8800] Poll exhausted after 100 attempts`)**:
   - The chip is not responding to our polling loops.
   - **Action**: Check `/tmp/radxa_ref/aic8800/aic8800_fdrv/aicwf_sdio.c` line-by-line against our `aicwf_sdiov3_func_init()`. Verify all SDIO register writes (`sleep_reg`, `wakeup_reg`, CCCR interrupt enables at `0x04`) match exactly.

3. **If RX packets enqueue but driver crashes / wlan0 fails to register**:
   - Data is flowing, but MAC layer parsing is failing.
   - **Action**: Trace the Radxa RX parsing path in `/tmp/radxa_ref/aic8800/aic8800_fdrv/aicwf_sdio.c` (`busrx_thread` -> `aicwf_sdio_enq_rxpkt` -> `rwnx_rx_handle_msg`) and verify our `shenmintao` driver isn't missing a critical `skb` conversion or header strip step.

4. **If RX packets enqueue and process, but kernel panics (Null Pointer / Use-After-Free)**:
   - The direct `skb_queue_tail` bypass in BUILD_130 might conflict with standard `dev_kfree_skb` destructors in `rwnx_rx_handle_msg`.
   - **Action**: Check if `rwnx_rx_handle_msg` expects `skb->cb` control buffer fields to be pre-populated by `aicwf_rxframe_enqueue`.

## Generating Patches and Commits
To compare our target code against the Radxa reference, the AI should use:
```bash
diff -u /tmp/radxa_ref/aic8800/aic8800_fdrv/FILE_NAME.c /home/tcmichals/projects/cubie/cubie-a5e/aic8800-driver-src/drivers/aic8800/aic8800_fdrv/FILE_NAME.c > /tmp/port_diff.patch
```
When creating commits for these fixes, use the following template:
```text
wifi: aic8800: [Short description of the Radxa port fix]

Ported logic from the working Radxa driver to resolve [describe the failure e.g., IPC timeouts]. 
Specifically, [describe the exact register write or loop logic changes].

Reference: Radxa vendor tree
Signed-off-by: [Your Name] <[Your Email]>
```

## Verification Checklist (Post-Flash)
Run these commands on the board to confirm success:
1. `dmesg | grep -i aic`
   - **EXPECTED**: `[aic8800] IPC read SUCCESS! chip_id=0x...`
   - **EXPECTED**: `enq_rxpkt len=512`
   - **EXPECTED**: `wlan0` registered.
2. `ip link show wlan0`
   - **EXPECTED**: The interface exists and has a valid MAC address.

## AI Instructions
1. When faced with an issue (e.g., timeout, kernel panic, or missing sequence), **always consult `/tmp/radxa_ref/`** first to see how the Radxa driver handles it.
2. Port the specific Radxa logic directly into the `shenmintao` source tree.
3. Keep this file updated with the latest state if we encounter new hardware bugs or make significant architectural decisions.

## Build & Test Log
*Use this section to track the chronological history of builds flashed and tested on the board.*

| Date | Build / Tag | Dmesg Output Summary | Next Action / Notes |
|---|---|---|---|
| 2026-07-28 | `BUILD_130_GUARANTEED_RX_ENQUEUE_FIX` | **Hardware RX SUCCESS**: `intstatus=0x01`, `enq_rxpkt len=512` received immediately! **Soft Timeout**: `aicwf_another_ptk()` returned `false` on raw SDIO bytes, dropping the payload before `rwnx_rx_handle_msg()`. | Fixed in BUILD_131 by adding raw hex dump & fallback CMD_RSP routing in `aicwf_txrxif.c`. |
| 2026-07-31 | `BUILD_131_RX_HEADER_UNWRAP_FIX` | **Root Cause Uncovered**: Log confirmed `aicwf_is_framequeue_empty` checked `skb_queue_head` while `aicwf_sdio_enq_rxpkt` enqueued to `list_head` under `CONFIG_PREALLOC_RX_SKB`. Queue was reported empty, preventing dequeue! | Fixed in BUILD_132 by adding `#ifdef CONFIG_PREALLOC_RX_SKB` handling with `list_empty` & `list_first_entry` in `aicwf_process_rxframes()`. |
| 2026-07-31 | `BUILD_132_PREALLOC_QUEUE_FIX` | **IPC & CHIP ID 100% SUCCESS**: `0x40504084` write SUCCESS, IPC read `0x40500000` SUCCESS (`memdata=0xfb078820 chip_id=0x07 chip_sub_id=0x02`), NVRAM loaded! **Timeout on Cmd 123**: `MM_SET_STACK_START_REQ` timed out because `cmd_mgr_queue` relied on dropped in-band IRQ. | Fixed in BUILD_133 by adding 100x2ms SDIO mailbox polling loop into `cmd_mgr_queue()` in `rwnx_cmds.c`. |
| 2026-07-31 | `BUILD_135_WORKQUEUE_POLLING_FIX` | **Log Diagnostic Confirmation**: Boot log confirmed `aicwf_sdio_tx_msg()` was running the polling loop, but missed `aicwf_process_rxframes(sdiodev->rx_priv)`, leaving `queue_sz == 1`. | Fixed in BUILD_136 by adding `aicwf_process_rxframes(sdiodev->rx_priv)` inside `aicwf_sdio_tx_msg()` polling loop. |
| 2026-07-31 | `BUILD_136_TX_MSG_PROCESS_RX_FIX` | **Timeout on Cmd 123**: The chip's initial boot sequence exceeded the hardcoded 200ms limit of our polling loop. Driver timed out before receiving the CFM response. | Fixed in BUILD_137 by replacing fixed-iteration loop with timeout-bounded polling loop in `cmd_mgr_queue()`. |
| 2026-08-01 | `BUILD_137_TIMEOUT_BOUNDED_POLLING` | **Root Cause Uncovered**: Log showed `[aic8800] MAC cmd 1035 CFM received on poll` BUT immediately followed by `cmd_mgr_queue cmd timed-out`! `wait_for_completion_killable_timeout` consumed `complete.done` (setting it to 0), causing subsequent `if (!completion_done(&cmd->complete))` check to evaluate `true` and declare a false timeout! | Fixed in BUILD_138 by tracking completion status in a boolean `cmd_completed` flag before calling `wait_for_completion_killable_timeout()`. |
| 2026-08-01 | `BUILD_138_COMPLETION_FLAG_FIX` | **Root Cause Uncovered**: IPC read (`0x40500000`) and command writes (`0x40504084`) both SUCCEEDED! But `cmd 123` (`MM_SET_STACK_START_REQ`) received no CFM because `PRODUCT_ID_AIC8800D80` fell through to the `else` branch in `rwnx_main.c`, passing invalid efuse/hwinfo parameters that crashed the ROM stack start. | Fixed in BUILD_139 by adding `PRODUCT_ID_AIC8800D80` to the explicit parameter block (`1, 0, CO_BIT(5)`), matching the Radxa driver. |
| 2026-08-01 | `BUILD_139_D80_STACK_START_PARAMS_FIX` | **Root Cause Uncovered**: Log confirmed `cmd 123` timed out because `aicwf_set_cmd_tx()` in `rwnx_cmds.c` was missing `PRODUCT_ID_AIC8800D80` from the `crc8_ponl_107()` calculation check. Byte 3 of the SDIO command packet header was transmitted as `0x00` instead of a valid CRC8 checksum, causing the chip hardware parser to drop `cmd 123` silently! | Fixed in BUILD_140 by adding `PRODUCT_ID_AIC8800D80` to the `crc8_ponl_107` header check in `aicwf_set_cmd_tx()`. |
| 2026-08-01 | `BUILD_145_V3_REG_MAP_FIX` | **Audit Discovery**: `aicwf_sdio.c` line 1360 was missing `PRODUCT_ID_AIC8800D80`, leaving `sdiodev->sdio_reg.sleep_reg` uninitialized (`0x00`). Every IRQ read register `0x00` instead of `0x04` (`SDIOWIFI_INTR_PENDING_REG_V3`), returning `intstatus=0x00`. | Fixed in BUILD_145 by populating V3 register map offsets for `PRODUCT_ID_AIC8800D80`. |
| 2026-08-01 | `BUILD_146_COMPLETE_CHIPID_AUDIT` | Hardware test confirmed: SDIO IRQ, RX processing, IPC read, and NVRAM loading are 100% SUCCESSFUL! `cmd 123` timed out. | Audit revealed two critical bugs: SDIO wakeup register check and OOB interrupt rerouting. |
| 2026-08-01 | `BUILD_147_SDIO_WAKEUP_REG_FIX` | `aicwf_sdio_wakeup()` was reading `sleep_reg` (0x04) instead of `wakeup_reg` (0x01) and checking `val & 0x10`. | Fixed in BUILD_147 by reading `wakeup_reg` (0x01) and checking `(val & 0x1) == 0` (matching Radxa line 1359). |
| 2026-08-01 | `BUILD_148_NO_OOB_REG_WRITE_FIX` | **Root Cause for `cmd 123` Timeout**: `system_config_8800d80` was writing `0x00000006` to `0x40504084`. This rerouted LMAC interrupts to the external OOB GPIO line instead of SDIO DAT1 bus! In Radxa, `CONFIG_OOB=n` so `0x40504084` is never written. | Fixed in BUILD_148 by removing the `0x40504084` write to keep interrupts on SDIO DAT1. |
| 2026-08-02 | `BUILD_158_ENFORCE_STRICT_PROBE_EXIT` | **Strict Probe Failure Alignment**: Probe correctly aborts on `err_lmac_reqs` if `cmd 123` (`MM_SET_STACK_START_REQ`) fails, preventing half-initialized driver state. | Bumped build to BUILD_158 and verified strict error return paths in `rwnx_main.c`. |






## AI Operational Directive
When this document is loaded into context, immediately parse the latest entry in the **Build & Test Log**. If the status is "Pending test", prompt the user for the latest `dmesg` output from the pending build. Do not write new code until the `dmesg` output is evaluated against the Actionable Plan.
