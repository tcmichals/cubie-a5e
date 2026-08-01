# Blueprint 6: Mainline Linux Wi-Fi Integration (AIC8800 Unified USB & SDIO Driver)

## 1. Mandated Rules & Workflow
* **SINGLE SOURCE OF TRUTH:** Always inspect `docs/buildroot/AIC8800_Porting_Notes.md` at the start of any work. Maintain all architectural changes, debugging histories, and hardware fixes in this document.
* **STRICTLY MAINLINE & BUS-AGNOSTIC:** The driver must compile and run on mainline Linux kernels (6.1+) using standard `cfg80211` / `mac80211` interfaces. Core logic must use the bus-agnostic `chipid` rather than transport-specific pointers (`usbdev->chipid`).
* **DUAL-TRANSPORT SUPPORT (SDIO & USB):** Maintain clean separation between the generic core (`rwnx_main.c`, `rwnx_platform.c`) and physical transport layers (`aicwf_sdio.c` for SDIO, `aicwf_usb.c` for USB).
* **MULTI-BOARD COMPATIBILITY (Cubie A5E & Cubie A7A):** Keep board-specific pin configurations (e.g. `host-wake` GPIOs, regulators, reset lines) inside board Device Tree overlays (`cubie-a5e-flight-stack.dtso`, `cubie-a7a-flight-stack.dtso`), while the driver uses dynamic DTS lookup (`of_find_compatible_node` / `of_irq_get_byname`).
* **DEFENSIVE UNINITIALIZED LIST & STRUCTURE VALIDATION:** Every workqueue callback, power-management routine (`aicwf_sdio_sleep_allow`), flow-control callback, and suspend/resume handler MUST defensively validate parent structures AND list head pointers (`!rwnx_hw->vifs.next || !rwnx_hw->vifs.prev`) before traversal, ensuring probe failures never trigger NULL pointer dereferences.
* **LINUX DEVICE TREE PROBING BASICS:** SDIO functions are children of MMC host controller nodes. Device tree property lookups (interrupts, host-wake, regulators) MUST target the explicit child device node (`of_find_compatible_node(..., "aic,aic8800")` or `of_get_child_by_name(mmc_np, "wifi")`) rather than passing the parent MMC host controller node pointer (`func->card->dev.of_node`), ensuring DT parsing never evaluates parent controller attributes by mistake.
* **NO PREMATURE BUS_DOWN_ST WORKQUEUE & IRQ GATING:** Never gate probe-phase control packets (`DBG_MEM_READ_REQ` / `DBG_MEM_READ_CFM`) or threaded IRQ handlers behind `bus_if->state == BUS_DOWN_ST`. During early probe, control packets and IRQ confirmations must transmit and receive freely over the hardware bus before `bus_if->state` transitions to `BUS_UP_ST`.
* **STRICT CHIP INIT ORDER OF OPERATIONS:** Always load firmware patch tables (`rwnx_platform_on()`) into internal chip RAM *before* issuing IPC memory read/write queries (`system_config()`), ensuring the LMAC firmware is running and capable of answering host IPC commands.
* **IDEMPOTENT FIRMWARE PATH CONCATENATION:** Global firmware path string appends (`aic_fw_path`) MUST be guarded with `!strstr()` substring checks (e.g. `if (!strstr(aic_fw_path, "aic8800D80N"))`) to prevent path duplication and invalid directory paths during driver re-probe or reset.
* **RESILIENT PROBE ERROR RECOVERY:** Never abort driver probe (`return;`) when early IPC memory reads (`0x40500000`) time out during initial boot before firmware patch tables are loaded. Fall back to default chip parameters (`chip_id = 0x80`), complete system register initialization, upload firmware patches into RAM, and register the `wlan0` netdev interface.
* **EMPIRICAL LOG-DRIVEN DIAGNOSIS:** Never offer repetitive speculative theories. Trace kernel log lines (`printk` / `dmesg`) to verify execution flow directly before making code changes.

## 2. Context & Architecture
* **SDIO Transport**: Uses Out-Of-Band (OOB) GPIO interrupts (`host-wake`) via `request_threaded_irq()` to bypass Allwinner `DAT1` in-band SDIO hardware bugs and eliminate `ksdioirqd` polling overhead.
* **USB Transport**: Uses standard Linux USB URBs (`usb_fill_bulk_urb`, `usb_submit_urb`) and control requests.
* **Wakeup Verification**: Enforces checking bit 4 (`val & 0x10`) of `sleep_reg` (`0x01`) in `aicwf_sdio_wakeup()` for D80/D81 series chips to ensure full wake status before memory reads (`0x40500000`).

## 3. Engineering Goals
1. Maintain unified compilation of `aic8800-driver` supporting both SDIO and USB devices on Buildroot.
2. Package required firmware binaries (`fw_patch_table_8800d80_u02.bin`, `aic8800DC`, etc.) into `/lib/firmware/aic8800/`.
3. Support seamless automatic interface bring-up (`wlan0`) across Cubie A5E and Cubie A7A boards.
4. **WINDOWS DRIVER AUDIT:** Cross-reference initialization sequences, register tables, and firmware patch parameters against vendor Windows driver archives (`peckishrine/aic8800_windows_drivers`) to ensure no chip init, power management, or calibration steps were missed.
5. **UPSTREAM CONTRIBUTION:** Prepare clean, well-formatted, bisect-friendly commits to submit back to the upstream `shenmintao/aic8800d80` repository, contributing our unified SDIO/USB refactoring, OOB GPIO interrupt integration, KUnit test suite, and modern kernel workqueue fixes back to the open-source community.

## 4. World-Class Driver Quality Standards
* **Zero Warning Compilation**: Driver must build cleanly with `-Wall -Wextra` under standard cross-compilation toolchains.
* **Zero Locking/Concurrency Violations**: Lock discipline must be strict (`spin_lock_irqsave` vs `mutex`), with no blocking `mdelay()` calls in atomic or SoftIRQ contexts.
* **Full Unit Test Coverage**: Maintain passing KUnit tests (`aicwf_bus_test`, `aicwf_rx_prealloc_test`, `aicwf_txq_prealloc_test`) for memory management, packet queues, and bus initialization.
* **Peer Code Review Ready**: Every commit must be bisect-friendly, self-contained, and accompanied by detailed commit messages adhering to Linux kernel coding style (`scripts/checkpatch.pl`).

## 5. Code Review & Debugging Protocol
* **UNCONDITIONAL KERNEL LOGGING (`pr_info` / `pr_err`)**: Milestone log messages (IRQ registration, DT node lookup, CMD send/receive, wake state) MUST use unconditional kernel logging (`pr_info("[aic8800] ...")`) rather than macro-suppressed debug prints, ensuring boot traces are always visible in `dmesg`.
* **AUTOMATED DUAL-DIFF CODE REVIEWS**: Before committing any refactor, perform an explicit diff against the original working Radxa vendor commit (`git diff 388a019`) to verify no initialization register, bitwise handshake, or timing delay was inadvertently removed.
* **DEFENSIVE ENTRY GUARDS IN ASYNC CALLBACKS**: Any function invoked by a timer (`timer_setup`), workqueue (`INIT_WORK`), or interrupt (`request_threaded_irq`) MUST validate `!sdiodev`, `!rwnx_hw`, and list pointers (`!vifs.next || !vifs.prev`) on entry.

## 6. Documentation & Maintenance
* Update `docs/buildroot/AIC8800_Porting_Notes.md` with every verified fix.
* Maintain `task.md` and `walkthrough.md` artifacts during active execution cycles.

