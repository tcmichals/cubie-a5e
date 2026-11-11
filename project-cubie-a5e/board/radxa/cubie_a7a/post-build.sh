#!/bin/sh
# post-build.sh — Post-build script for Radxa Cubie A7A rootfs assembly
# Ensures all custom target scripts in /usr/bin have executable permissions (+x).

TARGET_DIR="$1"

# /lib/firmware must come only from the riscv-firmware package. A copy left in the
# rootfs overlay (untracked, *.elf is gitignored) silently replaces the package build.
OVERLAY_FW="$(dirname "$0")/rootfs-overlay/lib/firmware"
if [ -d "${OVERLAY_FW}" ] && [ -n "$(ls -A "${OVERLAY_FW}")" ]; then
    echo "post-build: ${OVERLAY_FW} is not empty; firmware must come from the riscv-firmware package" >&2
    ls -la "${OVERLAY_FW}" >&2
    exit 1
fi

if [ -f "${TARGET_DIR}/usr/bin/probe_riscv_debug.sh" ]; then
    chmod 0755 "${TARGET_DIR}/usr/bin/probe_riscv_debug.sh"
fi

if [ -f "${TARGET_DIR}/usr/bin/npu-smoke-test" ]; then
    chmod 0755 "${TARGET_DIR}/usr/bin/npu-smoke-test"
fi

# Ensure /boot mount point directory exists for FAT boot partition automount
mkdir -p "${TARGET_DIR}/boot"

exit 0
