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
ctrl_interface=/var/run/wpa_supplicant
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
