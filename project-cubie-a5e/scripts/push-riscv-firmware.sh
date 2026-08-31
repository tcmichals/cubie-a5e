#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# push-riscv-firmware.sh
# Build, sync, and deploy RISC-V firmware to rootfs-overlay and/or live target board
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FIRMWARE_SRC="${WORKSPACE_ROOT}/cubie-a5e/riscv-firmware/apps/exampleRiscv"
FIRMWARE_ELF="${FIRMWARE_SRC}/firmware.elf"

# Target IP from argument or environment
TARGET_IP="${1:-$TARGET_IP}"

echo "========================================================"
echo "  Deploying RISC-V XuanTie E907 Firmware"
echo "========================================================"

# 1. Build RISC-V firmware if toolchain is available
if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 || command -v riscv-none-elf-gcc >/dev/null 2>&1; then
    echo "[1/4] Compiling RISC-V firmware in ${FIRMWARE_SRC}..."
    make -C "${FIRMWARE_SRC}" clean
    make -C "${FIRMWARE_SRC}" -j$(nproc)
else
    echo "[1/4] Using prebuilt firmware at ${FIRMWARE_ELF}"
fi

if [ ! -f "${FIRMWARE_ELF}" ]; then
    echo "ERROR: Firmware binary not found at ${FIRMWARE_ELF}"
    exit 1
fi

# 2. Sync to rootfs overlays for all board variants
echo "[2/4] Syncing ELF to rootfs-overlay..."
for board in cubie_a7a cubie_a7z cubie_a5e; do
    overlay_dir="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e/board/radxa/${board}/rootfs-overlay/lib/firmware"
    mkdir -p "${overlay_dir}"
    cp -v "${FIRMWARE_ELF}" "${overlay_dir}/riscv-firmware.elf"
done

# 3. Sync to active buildroot targets if they exist
echo "[3/4] Syncing ELF to active build targets..."
for bld in bld.a7a bld.a7a.test bld.a5e; do
    target_dir="${WORKSPACE_ROOT}/${bld}/target/lib/firmware"
    if [ -d "${target_dir}" ]; then
        cp -v "${FIRMWARE_ELF}" "${target_dir}/riscv-firmware.elf"
    fi
done

# 4. Push live to running target board via SSH/SCP if IP is supplied
if [ -n "${TARGET_IP}" ]; then
    echo "[4/4] Deploying live to target board at ${TARGET_IP}..."
    scp -o ConnectTimeout=5 -o StrictHostKeyChecking=no "${FIRMWARE_ELF}" "root@${TARGET_IP}:/lib/firmware/riscv-firmware.elf"
    
    echo "Restarting remoteproc on target..."
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@${TARGET_IP}" '
        if [ -f /etc/init.d/S60riscv ]; then
            /etc/init.d/S60riscv restart
        else
            echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
            sleep 1
            echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
            echo start > /sys/class/remoteproc/remoteproc0/state
        fi
        echo "Remote processor status:"
        cat /sys/class/remoteproc/remoteproc0/state
    '
    echo "Deployment to ${TARGET_IP} completed successfully!"
else
    echo "[4/4] No target IP provided. To deploy live to board, run:"
    echo "      $0 <board-ip-address>"
fi

echo "========================================================"
echo "Done!"
