# WirelessHowTo

This guide covers Wi-Fi bring-up for the Cubie A5E image in this repo.

## 1) Included components

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

## 2) Configure Wi-Fi credentials

Create `/etc/wpa_supplicant.conf` on target:

- set your SSID and PSK
- ensure permissions are restrictive

Recommended structure:

- `ctrl_interface=/var/run/wpa_supplicant`
- `update_config=1`
- `country=<2-letter code>`
- `network={...}` block with `ssid` + `psk`

## 3) Start and stop Wi-Fi service

On target:

- start: `/etc/init.d/S40network-wifi start`
- stop: `/etc/init.d/S40network-wifi stop`

## 4) Quick diagnostics

Useful checks:

- `ip link show wlan0`
- `iw dev`
- `ip addr show wlan0`
- `logread | grep -i -E "aic|wlan|wpa|dhcp"`

## 5) Common issues

- Missing `wlan0`: driver/firmware not loaded or module name mismatch
- No DHCP lease: AP credentials wrong, weak signal, or AP restrictions
- Auth failures: wrong `psk`, wrong country code/reg domain

## 6) Flight-use recommendation

For flight-controller roles, keep Wi-Fi optional for commissioning/telemetry and avoid making core control safety depend on link availability.
