# 📶 Common Subsystem Guide: AIC8800 Wi-Fi Architecture & Bring-Up

This document provides a unified reference for the **AIC8800 Wi-Fi 6 / Bluetooth 5.2** wireless subsystem used across the Radxa Cubie flight controller family. It details the dual-transport driver architecture, firmware loading stages, parameter tuning, and operational procedures.

---

## 1. Subsystem Architecture & Dual-Transport Model

The AIC8800D80 wireless chipset is integrated across both boards in the family, but is wired through different physical transports:

| Hardware Target | Host SoC | Physical Bus | Kernel Driver Modules | Device ID / Interface |
| :--- | :--- | :--- | :--- | :--- |
| **Radxa Cubie A5E** | Allwinner A527 / T527 (`sun55i`) | **SDIO 3.0** (4-bit, 50 MHz) | `aic8800_bsp.ko` + `aic8800_fdrv.ko` | `mmc1` / `wlan0` |
| **Radxa Cubie A7A** | Allwinner A733 (`sun60iw2`) | **USB 2.0 High-Speed** | `aic8800_fdrv.ko` (Standalone) | `0xA69C:0x8800` / `wlan0` |

---

## 2. Unified Driver Design (`aic8800-upstream`)

Rather than maintaining separate, fragmented vendor driver repositories, the workspace maintains a single, unified codebase in [`aic8800-upstream/`](/aic8800-upstream/):

1. **Transport Abstraction Layer**:
   - `aicwf_sdio.c` / `aicwf_sdiov3.c`: Handles SDIO block transfers, CCCR interrupt enable, sleep/wakeup registers, and `aic8800_bsp` registration.
   - `aicwf_usb.c` / `usb_host.c`: Implements standard Linux `usbcore` registration, asynchronous URB anchors, dynamic `skb` allocation, and vendor control pipe transactions.
2. **Bus-Agnostic Core MAC & Management**:
   - `rwnx_platform.c`: Implements `rwnx_platform_get_dev()` and `rwnx_platform_get_hw()` so higher-level MAC functions (`rwnx_tx.c`, `rwnx_main.c`, `rwnx_rx.c`) operate identically regardless of underlying bus.
3. **Buildroot Build Integration**:
   - For **A5E** (`bld.a5e`): Configured with `CONFIG_SDIO_SUPPORT=y` (compiles both `aic8800_bsp.ko` and `aic8800_fdrv.ko`).
   - For **A7A** (`bld.a7a`): Configured with `CONFIG_USB_SUPPORT=y` (compiles a single standalone `aic8800_fdrv.ko` module).

---

## 3. Firmware Upload Stages & Initialization Flow

When the kernel module probes, the driver executes a strict multi-stage initialization sequence:

```
   ┌──────────────────────────────────────────────────────────┐
   │ 1. Read Chip Info / IPC Register (0x40500000)             │
   │    • Identify PRODUCT_ID_AIC8800D80                      │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 2. Sequential Binary Firmware Upload                      │
   │    • fw_adid_8800d80_u02.bin   (Analog Die ID table)     │
   │    • fw_patch_8800d80_u02.bin  (ROM patch microcode)     │
   │    • fmacfw_8800d80_u02.bin    (FullMAC firmware binary) │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 3. Patch Table & FMAC Configuration                      │
   │    • aicwifi_patch_config_8800d80() writes PTCH magic    │
   │    • Load userconfig: /lib/firmware/aic_userconfig.txt   │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 4. Start Application & ROM Stack Start                   │
   │    • start_app write to chip RAM (0x00210000)            │
   │    • MM_SET_STACK_START_REQ (cmd 123, vendor_info = 0)   │
   │    • rwnx_ic_rf_init() (RF calibration & TX power set)   │
   │    • Register wlan0 net_device with Linux mac80211/cfg80211│
   └──────────────────────────────────────────────────────────┘
```

---

## 4. Known Pitfalls & Solutions
 
1. **SDIO Wakeup Register (`BUILD_147_SDIO_WAKEUP_REG_FIX`)**:
   - *Issue*: Reading `sleep_reg` (0x04) instead of `wakeup_reg` (0x01) caused SDIO wakeup timeouts.
   - *Fix*: Read `wakeup_reg` (0x01) and verify `(val & 0x1) == 0`.
2. **Out-of-Band (OOB) Interrupt Trap (`BUILD_148_NO_OOB_REG_WRITE_FIX`)**:
   - *Issue*: Writing `0x00000006` to `0x40504084` rerouted interrupts to an unconnected physical GPIO pin instead of the SDIO DAT1 bus.
   - *Fix*: Disabled the `0x40504084` write when `CONFIG_OOB=n`.
3. **Firmware Upload Order (`BUILD_167_RESTORE_PROPER_STACK_START_ORDER`)**:
   - *Issue*: Triggering `rwnx_ic_rf_init()` *before* `cmd 123` (`MM_SET_STACK_START_REQ`) timed out because ROM LMAC was inactive.
   - *Fix*: Enforce order: `rwnx_ic_system_init()` $\rightarrow$ `cmd 123` $\rightarrow$ `rwnx_ic_rf_init()`.
4. **USB FullMAC Endpoint Mapping & Command Confirmation Routing**:
   - *Issue*: On USB FullMAC (`a69c:8d80`), `wlan0` scanning hung with `aic8800_fdrv: command timeout (6300 ms)` and returned `-16` (`-EBUSY`).
   - *Root Cause*: Interface 2 defines 4 bulk endpoints:
     - `ep_01` (OUT) / `ep_81` (IN) for network data frames.
     - `ep_02` (OUT) / `ep_82` (IN) for internal firmware control messages.
     Incoming control responses on `ep_82` were routed through `aicwf_process_rxframes()` as regular data packets instead of parsing them with `rwnx_rx_handle_msg()`.
   - *Fix*: Implemented `aicwf_process_msg_rxframes()` in `aicwf_usb.c` and hooked the `msg_rx_work` queue directly to `rwnx_rx_handle_msg()`. Firmware queries (`FW Version`) now respond in **17 ms** instead of 6,300 ms.
5. **Silent Outgoing Data & EAPOL Drop on USB**:
   - *Issue*: Scanning succeeded, but WPA2/WPA3 association timed out with AP deauth `reason 15 (4WAY_HANDSHAKE_TIMEOUT)`. `iw dev wlan0 link` showed `TX: 0 bytes`.
   - *Root Cause*: In `rwnx_tx.c::rwnx_tx_push()`, frame dispatch was wrapped strictly in `#ifdef AICWF_SDIO_SUPPORT`. When compiling for USB, the entire transmission block was compiled out, silently freeing outgoing skbs.
   - *Fix*: Implemented `#elif defined(AICWF_USB_SUPPORT)` branch connecting `aicwf_usb_host_txdesc_push()` to `rwnx_hw->usb_env` and dispatching packets via `aicwf_frame_tx((void *)(rwnx_hw->usbdev), skb)`.
6. **Dynamic Buffer Sizing in `aicwf_usb.c`**:
   - *Issue*: Potential memory truncation when transmitting confirm-requested packets (`need_cfm`).
   - *Fix*: Sized allocation to `skb->len + sizeof(usb_header) + sizeof(struct txdesc_api) + TX_ALIGNMENT` and pruned unused raw pointer dereferences in `aicwf_usb_tx_process()`.

---

## 5. Network Configuration & Silicon Validation

1. **Bring Up the Interface**:
   ```bash
   ip link set wlan0 up
   ```

2. **Connect to Access Point (`wpa_supplicant`)**:
   ```bash
   wpa_passphrase "SSID" "PASSPHRASE" >> /etc/wpa_supplicant.conf
   wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf
   udhcpc -i wlan0
   ```

3. **Verify Signal & Link Metrics**:
   ```bash
   iw dev wlan0 link
   ping -I wlan0 -c 5 192.168.1.1
   ping -I wlan0 -c 5 8.8.8.8
   ```

---

## 6. Live Silicon Performance & Parity Matrix

The unified driver has been verified on physical hardware across both platforms with zero regressions:

| Parameter | Radxa Cubie A7A (USB 2.0) | Radxa Cubie A5E (SDIO) |
| :--- | :--- | :--- |
| **SoC / Bus** | Allwinner A733 / High-Speed USB 2.0 | Allwinner T527 / SDIO 3.0 (OOB IRQ 180) |
| **PHY Device ID** | `0xA69C:0x8D80` (Downstream Hub Port 4) | `mmc1` SDIO Function 1 (`0xA69C:0x8D80`) |
| **Firmware Bootstrap** | Instant (~100 ms) | Fast (~300 ms) |
| **Wi-Fi Association** | WPA2-PSK (`NETGEAR69-5G`, 5 GHz, 80 MHz) | WPA2-PSK (`NETGEAR69-5G`, 5 GHz, 80 MHz) |
| **PHY Link Rate** | **433.3 Mbps RX** / **234.0 Mbps TX** | **433.3 Mbps RX** / **351.0 Mbps TX** |
| **Gateway Ping RTT** | **0.58 ms avg** (0% loss) | **0.93 ms min / 1.25 ms avg** (0% loss) |
| **Internet Ping RTT** | **14.2 ms avg** (0% loss to 8.8.8.8) | **13.8 ms min / 15.8 ms avg** (0% loss to 8.8.8.8) |
| **Throughput Stress** | >30 MB sustained transfer | **50 MB sustained transfer** (65.8 MB RX total) |
| **Kernel / Driver Health** | 0 drops, 0 dmesg errors | 0 drops, 0 CRC errors, 0 dmesg errors |
| **Git Release Tag** | `v1.0.3-unified-dual-bus-wifi6-silicon-verified` | `v1.0.3-unified-dual-bus-wifi6-silicon-verified` |
