#!/bin/sh
# post-build.sh — Post-build script for Radxa Cubie A7A rootfs assembly
# Ensures all custom target scripts in /usr/bin have executable permissions (+x).

TARGET_DIR="$1"

if [ -f "${TARGET_DIR}/usr/bin/probe_riscv_debug.sh" ]; then
    chmod 0755 "${TARGET_DIR}/usr/bin/probe_riscv_debug.sh"
fi

if [ -f "${TARGET_DIR}/usr/bin/npu-smoke-test" ]; then
    chmod 0755 "${TARGET_DIR}/usr/bin/npu-smoke-test"
fi

# Ensure /boot mount point directory exists for FAT boot partition automount
mkdir -p "${TARGET_DIR}/boot"

exit 0
