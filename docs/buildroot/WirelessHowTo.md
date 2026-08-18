# WirelessHowTo (Dual-Bus: SDIO & USB)

This guide covers Wi-Fi bring-up for both target platforms in this repository:
1. **Radxa Cubie A5E** (Allwinner A527/T527) via **SDIO**
2. **Radxa Cubie A7A** (Allwinner A733) via **USB**

---

## 1) Hardware Wiring and Transport Configurations

### A. Radxa Cubie A5E (SDIO Transport)
The onboard AIC8800D80 Wi-Fi chip on Cubie A5E connects over SDIO (`mmc1`):
- **SDIO Interface**: Connected to `mmc1`, `bus-width = <4>`, marked `non-removable`.
- **Power (VCC)**: `3v3-wifi` regulator enabled via `PIO 0 7` (assigned to `vmmc-supply`).
- **I/O Power (VCC-IO)**: Internal `reg_bldo1` (`vcc-pg-iowifi`), assigned to `vqmmc-supply`.
- **Reset Sequence**: Controlled by `mmc-pwrseq-simple` node driving `PIO 1 1` low.
- **Interrupts**: Host wake-up interrupt on `PIO 1 0` (active low).
- **Modules Loaded**: `aic8800_bsp.ko` + `aic8800_fdrv.ko`.

### B. Radxa Cubie A7A (USB Transport)
The AIC8800 Wi-Fi chip on Cubie A7A is wired to the internal USB controller:
- **USB Device ID**: `0xA69C:0x8800` (Wi-Fi only) or `0xA69C:0x8801` (Wi-Fi + BT Combo).
- **Driver Architecture**: Native `usbcore` registration using standalone `aic8800_fdrv.ko` (no BSP module required).
- **Module Loaded**: `aic8800_fdrv.ko`.

## 2) Included components

From `cubie_a5e_defconfig`:

- `BR2_PACKAGE_LINUX_FIRMWARE=y`
- `BR2_PACKAGE_WPA_SUPPLICANT=y`
- `BR2_PACKAGE_IW=y`
- `BR2_PACKAGE_WIRELESS_TOOLS=y`
- out-of-tree packages: `aic8800-driver`, `aic8800-firmware`

Boot init script in rootfs overlay:

- `/etc/init.d/S40network-wifi`

The script does:

- `modprobe aic8800_fdrv`
- `wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf`
- `udhcpc -b -i wlan0 -R`

## 3) Configure Wi-Fi credentials

### Step A: Bring the interface up and scan for networks
Before connecting, verify the radio is up and scan for your Access Point:
```bash
# Bring the interface up
ip link set wlan0 up

# Scan for available Wi-Fi SSIDs
iw wlan0 scan | grep SSID
```

### Step B: Generate the configuration file securely
Use the `wpa_passphrase` tool to generate an encrypted network configuration block. This prevents storing your raw plain-text password on the device filesystem:
```bash
# Generate the base configuration with encrypted PSK
wpa_passphrase "Your_SSID" "Your_Password" > /etc/wpa_supplicant.conf
```

### Step C: Add global settings to the configuration
Open `/etc/wpa_supplicant.conf` and ensure the global parameters (like control interface and country code) are defined at the top of the file:
```text
ctrl_interface=DIR=/var/run/wpa_supplicant
update_config=1
country=US

network={
    ssid="Your_SSID"
    #psk="Your_Password"
    psk=7a77f9872bd77f8976a402324976a402f06b12f65a1c32729d7272fb658390ab
}
```
*(You can safely delete the commented `#psk="Your_Password"` line to keep the system secure).*

### Step D: Secure the configuration file permissions
Ensure that only the `root` user can read or modify the credentials file:
```bash
chmod 600 /etc/wpa_supplicant.conf
```


## 4) Start and stop Wi-Fi service

On target:

- start: `/etc/init.d/S40network-wifi start`
- stop: `/etc/init.d/S40network-wifi stop`

## 5) Upstream Driver Architecture & Mainline Patches

The Wi-Fi stack uses the official `wireless-next` RFC v2 driver tree (`aic8800_bsp.ko` + `aic8800_fdrv.ko`) located in `aic8800-upstream/`, patched for modern Linux 7.1+ and Allwinner SDIO controller timing.

The patch series is codified under `docs/upstream_patches/`:
1. `0001-wifi-aic-fix-stack-buffer-overflow-in-aicbt_patch_i.patch`: Bounds checks `memcpy` in `aicbt_patch_info_unpack()` to prevent `-fstack-protector` panics.
2. `0002-wifi-aic-update-cfg80211-API-compatibility-for-mode.patch`: Modernizes callback signatures (`netif_rx`, `cfg80211_probe_status`, `remain_on_channel`) for Linux 6.x/7.x.
3. `0003-wifi-aic-fix-sdio-phase-timing-and-wakeup-sequence.patch`: Guards IOPAD delay registers (`0xF0`, `0xF8`, `0xF1`) to prevent MMC data errors on 25 MHz SDIO, calls explicit chip wakeup on probe, and provides safe fallback if pre-boot `0x40500000` IPC is unavailable.

## 6) Quick Diagnostics

Useful checks on the target device:

```bash
# Verify kernel module load and hardware state
dmesg | grep -iE "aic|wlan|mmc1"

# Check interface link state and MAC address
ip link show wlan0

# Check wireless physical capabilities (HT / VHT / HE Wi-Fi 6)
iw phy0 info

# Check assigned IP and routing
ip addr show wlan0
ip route show

# Scan for access points
iw dev wlan0 scan | grep SSID
```

## 7) Common Issues and Solutions

- **`sunxi-mmc: data error, sending stop command`**: Caused by incorrect IOPAD phase delays on SDR/High-Speed modes. Resolved in Patch 3 (`aicwf_sdiov3_func_init`).
- **`regulatory.db failed with error -2`**: Missing `BR2_PACKAGE_WIRELESS_REGDB=y` in Buildroot defconfig.
- **`cmd:1024 timed-out`**: Occurs if the chip is in sleep mode when probe begins. Resolved by explicit `aicwf_sdio_wakeup()` in probe sequence.
- **No DHCP lease**: Check AP credentials in `/etc/wpa_supplicant.conf` or verify signal strength with `iw wlan0 scan`.

## 8) Flight-use recommendation

For flight-controller roles, keep Wi-Fi optional for commissioning/telemetry and avoid making core control safety depend on link availability.
