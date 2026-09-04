#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# push-riscv-firmware.sh
# Build, sync, and deploy all XuanTie E907 RISC-V firmware ELFs to rootfs-overlay and target board
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FIRMWARE_DIR="${WORKSPACE_ROOT}/cubie-a5e/riscv-firmware"
BIN_DIR="${FIRMWARE_DIR}/bin"

# Target IP from argument or environment
TARGET_IP="${1:-$TARGET_IP}"
DEFAULT_FW="${2:-testBasicTrace0.elf}"

echo "========================================================"
echo "  Deploying All XuanTie E907 RISC-V Firmware ELFs"
echo "========================================================"

# 1. Build all firmware applications & host tools
echo "[1/4] Compiling all RISC-V applications in ${FIRMWARE_DIR}..."
make -C "${FIRMWARE_DIR}" clean
make -C "${FIRMWARE_DIR}" -j$(nproc)

if [ ! -d "${BIN_DIR}" ]; then
    echo "ERROR: Output directory not found at ${BIN_DIR}"
    exit 1
fi

# 2. Sync all ELFs to rootfs overlay for Radxa Cubie A5E
echo "[2/4] Syncing all firmware ELFs to rootfs-overlay..."
for board in cubie_a5e; do
    overlay_dir="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e/board/radxa/${board}/rootfs-overlay/lib/firmware"
    mkdir -p "${overlay_dir}"
    cp -v "${BIN_DIR}"/*.elf "${overlay_dir}/"
    cp -v "${BIN_DIR}/${DEFAULT_FW}" "${overlay_dir}/riscv-firmware.elf"
    
    # Copy host tools to rootfs overlay /usr/local/bin
    tools_dir="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e/board/radxa/${board}/rootfs-overlay/usr/local/bin"
    mkdir -p "${tools_dir}"
    if [ -f "${BIN_DIR}/ping_shm" ]; then
        cp -v "${BIN_DIR}/ping_shm" "${tools_dir}/"
    fi
    if [ -f "${BIN_DIR}/ping_rpmsg" ]; then
        cp -v "${BIN_DIR}/ping_rpmsg" "${tools_dir}/"
    fi
    if [ -f "${BIN_DIR}/ping_dram" ]; then
        cp -v "${BIN_DIR}/ping_dram" "${tools_dir}/"
    fi
done

# 3. Sync to active buildroot target directories if they exist
echo "[3/4] Syncing to active buildroot targets..."
for bld in bld.a5e; do
    target_fw="${WORKSPACE_ROOT}/${bld}/target/lib/firmware"
    target_bin="${WORKSPACE_ROOT}/${bld}/target/usr/local/bin"
    if [ -d "${target_fw}" ]; then
        cp -v "${BIN_DIR}"/*.elf "${target_fw}/"
        cp -v "${BIN_DIR}/${DEFAULT_FW}" "${target_fw}/riscv-firmware.elf"
    fi
    if [ -d "${target_bin}" ]; then
        [ -f "${BIN_DIR}/ping_shm" ] && cp -v "${BIN_DIR}/ping_shm" "${target_bin}/"
        [ -f "${BIN_DIR}/ping_rpmsg" ] && cp -v "${BIN_DIR}/ping_rpmsg" "${target_bin}/"
        [ -f "${BIN_DIR}/ping_dram" ] && cp -v "${BIN_DIR}/ping_dram" "${target_bin}/"
    fi
done

# 4. Push live to running target board via SSH/SCP if IP is supplied
if [ -n "${TARGET_IP}" ]; then
    echo "[4/4] Deploying live to target board at ${TARGET_IP}..."
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@${TARGET_IP}" "mkdir -p /lib/firmware /usr/local/bin"
    scp -o ConnectTimeout=5 -o StrictHostKeyChecking=no "${BIN_DIR}"/*.elf "root@${TARGET_IP}:/lib/firmware/"
    scp -o ConnectTimeout=5 -o StrictHostKeyChecking=no "${BIN_DIR}/ping_shm" "${BIN_DIR}/ping_rpmsg" "${BIN_DIR}/ping_dram" "root@${TARGET_IP}:/usr/local/bin/" 2>/dev/null || true

    
    echo "Restarting remoteproc on target with default firmware: ${DEFAULT_FW}..."
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@${TARGET_IP}" "
        echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
        sleep 1
        echo \"${DEFAULT_FW}\" > /sys/class/remoteproc/remoteproc0/firmware
        echo start > /sys/class/remoteproc/remoteproc0/state
        echo 'Remote processor status:'
        cat /sys/class/remoteproc/remoteproc0/state
    "
    echo "========================================================"
    echo " Deployment to ${TARGET_IP} completed successfully!"
    echo " To switch to another firmware on target:"
    echo "   echo stop > /sys/class/remoteproc/remoteproc0/state"
    echo "   echo \"testPing.elf\" > /sys/class/remoteproc/remoteproc0/firmware"
    echo "   echo start > /sys/class/remoteproc/remoteproc0/state"
    echo "========================================================"
else
    echo "[4/4] No target IP provided. To deploy live to board, run:"
    echo "      $0 <board-ip-address> [firmware-name.elf]"
fi

echo "========================================================"
echo "Done!"
