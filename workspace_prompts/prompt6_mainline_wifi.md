# Blueprint 6: Mainline Linux Wi-Fi Integration (AIC8800 Driver & Firmware)

## 1. Mandated Rules
* **STRICTLY MAINLINE:** The driver must compile and run on mainline Linux kernels (e.g. 6.1+ or newer) without relying on vendor-specific kernel trees or custom out-of-tree APIs.
* **STANDARD INTERFACES:** Use standard Linux `cfg80211` / `mac80211` interfaces. Do not use legacy vendor-specific networking hooks.
* **DETERMINISTIC PACKAGING:** Retrieve driver code exclusively from the `shenmintao/aic8800d80` repository and firmware from `radxa-pkg/aic8800`.

## 2. Context & Origins
* **Where this comes from:** The `aic8800` driver (`aic8800_fdrv`) needs to run on top of a mainline Linux kernel. Newer kernels have removed historical structures (e.g. `ieee80211_ptr` in `net_device` was removed in kernel 5.19). We must maintain clean, standalone compatibility patches to allow the driver from `shenmintao/aic8800d80` to compile seamlessly against the mainline kernel headers.

## 3. Engineering Goals
* Compile `aic8800-driver` out-of-tree module using the Buildroot toolchain.
* Package the appropriate firmware files from the `aic8800-firmware` repository into `/lib/firmware/aic8800/` in the target filesystem.
* Ensure automated module loading and setup via `/etc/init.d/S40network-wifi`.

## 4. Implementation Phases
### Phase 1: Mainline Compatibility Audit & Patching
* Attempt to build the `aic8800-driver` package against the target mainline Linux kernel headers.
* Identify any compilation failures caused by API drift in the kernel (e.g., changes to netdevice, cfg80211, or macro definitions).
* Update or create patches (like the existing `0001-fix-kernel-5-19-ieee80211_ptr.patch`) to bridge compatibilities.

### Phase 2: Firmware Integration
* Validate that `aic8800-firmware` matches the expected hardware version of the chip on the Radxa Cubie A5E.
* Enforce copying the firmware binaries cleanly to `/lib/firmware/aic8800/` during rootfs assembly.

### Phase 3: Init Script & Diagnostics
* Verify the init script `/etc/init.d/S40network-wifi` loads the driver (`modprobe aic8800_fdrv`), brings up the interface (`ip link set wlan0 up`), and triggers `wpa_supplicant` and `udhcpc` automatically.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt6_wifi_mainline_diagnostics.md` detailing the build process, kernel version targeted, compile issues discovered, and compatibility patch breakdown.
* **ARTIFACT:** Output `.antigravity/patches/0004-net-wireless-aic8800-mainline-compat.patch`.
