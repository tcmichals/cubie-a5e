# WirelessHowTo

This guide covers Wi-Fi bring-up for the Cubie A5E image in this repo.

## 1) Hardware Wiring and Device Tree Configuration

The onboard AIC8800 Wi-Fi chip operates over the SDIO interface. It is crucial that the Device Tree is correctly configured to power up and reset the chip so the MMC subsystem can probe it.

The chip is wired as follows:
- **SDIO Interface**: Connected to `mmc1`. It requires `bus-width = <4>` and must be marked as `non-removable`.
- **Power (VCC)**: A dedicated `3v3-wifi` regulator must be enabled by driving `PIO 0 7` high. This regulator is assigned to `vmmc-supply`.
- **I/O Power (VCC-IO)**: Driven by the internal `reg_bldo1` (`vcc-pg-iowifi`), assigned to `vqmmc-supply`.
- **Reset Sequence**: Controlled by a `mmc-pwrseq-simple` node. The chip is pulled out of reset by driving `PIO 1 1` low.
- **Interrupts**: The host wake-up interrupt is wired to `PIO 1 0` (active low).

A proper overlay configures `&mmc1` with these regulators and power sequences to ensure the kernel detects the SDIO card at boot.

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

## 5) Quick diagnostics

Useful checks:

- `ip link show wlan0`
- `iw dev`
- `ip addr show wlan0`
- `logread | grep -i -E "aic|wlan|wpa|dhcp"`

## 6) Common issues

- Missing `wlan0`: driver/firmware not loaded or module name mismatch
- No DHCP lease: AP credentials wrong, weak signal, or AP restrictions
- Auth failures: wrong `psk`, wrong country code/reg domain

## 7) Flight-use recommendation

For flight-controller roles, keep Wi-Fi optional for commissioning/telemetry and avoid making core control safety depend on link availability.
