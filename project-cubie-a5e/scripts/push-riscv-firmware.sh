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

# Parse arguments: support <board-ip> [--test] [firmware.elf] in any order
TARGET_IP=""
DEFAULT_FW="testBasic.elf"
RUN_TESTS=0

for arg in "$@"; do
    case "$arg" in
        --test)
            RUN_TESTS=1
            ;;
        *.elf)
            DEFAULT_FW="$arg"
            ;;
        *)
            if [ -z "$TARGET_IP" ]; then
                TARGET_IP="$arg"
            fi
            ;;
    esac
done

echo "========================================================"
echo "  Deploying All XuanTie E907 RISC-V Firmware ELFs"
if [ -n "${TARGET_IP}" ]; then
    echo "  Target IP: ${TARGET_IP}"
    if [ "${RUN_TESTS}" = "1" ]; then
        echo "  Mode: Deploy & Execute Test Suite"
    else
        echo "  Default Boot Firmware: ${DEFAULT_FW}"
    fi
fi
echo "========================================================"

# 1. Build all firmware applications & host tools
echo "[1/4] Compiling all RISC-V applications in ${FIRMWARE_DIR}..."
if [ -z "${HOST_CXX:-}" ]; then
    AARCH64_GXX="${WORKSPACE_ROOT}/bld.a5e/host/bin/aarch64-linux-g++"
    if [ -x "${AARCH64_GXX}" ]; then
        export HOST_CXX="${AARCH64_GXX}"
        echo "  -> Using ARM64 Cross-Compiler for Host Tools: ${HOST_CXX}"
    fi
fi
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
    
    # Copy host tools & python telemetry scripts to rootfs overlay /usr/bin
    for sub in usr/bin; do
        tools_dir="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e/board/radxa/${board}/rootfs-overlay/${sub}"
        mkdir -p "${tools_dir}"
        [ -f "${BIN_DIR}/ping_shm" ] && cp -v "${BIN_DIR}/ping_shm" "${tools_dir}/"
        [ -f "${BIN_DIR}/ping_uio" ] && cp -v "${BIN_DIR}/ping_uio" "${tools_dir}/"
        [ -f "${BIN_DIR}/ping_uio.py" ] && cp -v "${BIN_DIR}/ping_uio.py" "${tools_dir}/" && chmod +x "${tools_dir}/ping_uio.py"
        [ -f "${BIN_DIR}/ping_rpmsg" ] && cp -v "${BIN_DIR}/ping_rpmsg" "${tools_dir}/"
        [ -f "${BIN_DIR}/ping_rpmsg.py" ] && cp -v "${BIN_DIR}/ping_rpmsg.py" "${tools_dir}/" && chmod +x "${tools_dir}/ping_rpmsg.py"
        [ -f "${BIN_DIR}/ping_dram" ] && cp -v "${BIN_DIR}/ping_dram" "${tools_dir}/"
        if [ -f "${BIN_DIR}/monitor_trace.py" ]; then
            cp -v "${BIN_DIR}/monitor_trace.py" "${tools_dir}/"
            chmod +x "${tools_dir}/monitor_trace.py"
        fi
        if [ -f "${BIN_DIR}/fast_sram_telemetry.py" ]; then
            cp -v "${BIN_DIR}/fast_sram_telemetry.py" "${tools_dir}/"
            chmod +x "${tools_dir}/fast_sram_telemetry.py"
        fi
        if [ -f "${BIN_DIR}/e907_mem.py" ]; then
            cp -v "${BIN_DIR}/e907_mem.py" "${tools_dir}/"
            chmod +x "${tools_dir}/e907_mem.py"
        fi
        if [ -f "${BIN_DIR}/run_tests.py" ]; then
            cp -v "${BIN_DIR}/run_tests.py" "${tools_dir}/"
            chmod +x "${tools_dir}/run_tests.py"
        fi
        cp -v "${BIN_DIR}"/*_devmem.* "${tools_dir}/" 2>/dev/null || true
    done
done

# 3. Sync to active buildroot target directories if they exist
echo "[3/4] Syncing to active buildroot targets..."
for bld in bld.a5e; do
    target_fw="${WORKSPACE_ROOT}/${bld}/target/lib/firmware"
    target_bin="${WORKSPACE_ROOT}/${bld}/target/usr/bin"
    target_local_bin="${WORKSPACE_ROOT}/${bld}/target/usr/local/bin"
    if [ -d "${target_fw}" ]; then
        cp -v "${BIN_DIR}"/*.elf "${target_fw}/"
        cp -v "${BIN_DIR}/${DEFAULT_FW}" "${target_fw}/riscv-firmware.elf"
    fi
    for tbin in "${target_bin}" "${target_local_bin}"; do
        if [ -d "${tbin}" ]; then
            [ -f "${BIN_DIR}/ping_shm" ] && cp -v "${BIN_DIR}/ping_shm" "${tbin}/"
            [ -f "${BIN_DIR}/ping_uio" ] && cp -v "${BIN_DIR}/ping_uio" "${tbin}/"
            [ -f "${BIN_DIR}/ping_uio.py" ] && cp -v "${BIN_DIR}/ping_uio.py" "${tbin}/" && chmod +x "${tbin}/ping_uio.py"
            [ -f "${BIN_DIR}/ping_rpmsg" ] && cp -v "${BIN_DIR}/ping_rpmsg" "${tbin}/"
            [ -f "${BIN_DIR}/ping_rpmsg.py" ] && cp -v "${BIN_DIR}/ping_rpmsg.py" "${tbin}/" && chmod +x "${tbin}/ping_rpmsg.py"
            [ -f "${BIN_DIR}/ping_dram" ] && cp -v "${BIN_DIR}/ping_dram" "${tbin}/"
            if [ -f "${BIN_DIR}/monitor_trace.py" ]; then
                cp -v "${BIN_DIR}/monitor_trace.py" "${tbin}/"
                chmod +x "${tbin}/monitor_trace.py"
            fi
            if [ -f "${BIN_DIR}/fast_sram_telemetry.py" ]; then
                cp -v "${BIN_DIR}/fast_sram_telemetry.py" "${tbin}/"
                chmod +x "${tbin}/fast_sram_telemetry.py"
            fi
        fi
    done
done

# 4. Push live to running target board via SSH/SCP if IP is supplied
if [ -n "${TARGET_IP}" ]; then
    echo "[4/4] Deploying live to target board at ${TARGET_IP}..."
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@${TARGET_IP}" "mkdir -p /lib/firmware /usr/bin /usr/local/bin"
    scp -O -o ConnectTimeout=5 -o StrictHostKeyChecking=no "${BIN_DIR}"/*.elf "root@${TARGET_IP}:/lib/firmware/"
    scp -O -o ConnectTimeout=5 -o StrictHostKeyChecking=no \
        "${BIN_DIR}/ping_shm" \
        "${BIN_DIR}/ping_uio" \
        "${BIN_DIR}/ping_uio.py" \
        "${BIN_DIR}/ping_rpmsg" \
        "${BIN_DIR}/ping_rpmsg.py" \
        "${BIN_DIR}/ping_dram" \
        "${BIN_DIR}/monitor_trace.py" \
        "${BIN_DIR}/fast_sram_telemetry.py" \
        "${BIN_DIR}/e907_mem.py" \
        "${BIN_DIR}/run_tests.py" \
        ${BIN_DIR}/*_devmem.* \
        "root@${TARGET_IP}:/usr/bin/" 2>/dev/null || true
    scp -O -o ConnectTimeout=5 -o StrictHostKeyChecking=no \
        "${BIN_DIR}/ping_shm" \
        "${BIN_DIR}/ping_uio" \
        "${BIN_DIR}/ping_uio.py" \
        "${BIN_DIR}/ping_rpmsg" \
        "${BIN_DIR}/ping_rpmsg.py" \
        "${BIN_DIR}/ping_dram" \
        "${BIN_DIR}/monitor_trace.py" \
        "${BIN_DIR}/fast_sram_telemetry.py" \
        "${BIN_DIR}/e907_mem.py" \
        "${BIN_DIR}/run_tests.py" \
        ${BIN_DIR}/*_devmem.* \
        "root@${TARGET_IP}:/usr/local/bin/" 2>/dev/null || true

    if [ "${RUN_TESTS}" = "1" ]; then
        echo "========================================================"
        echo "  Executing Automated E907 Validation Suite on Target"
        echo "========================================================"
        ssh -t -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@${TARGET_IP}" "python3 /usr/bin/run_tests.py"
    else
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
        echo " To run the automated test suite on target:"
        echo "   python3 /usr/bin/run_tests.py"
        echo " Or from host:"
        echo "   $0 ${TARGET_IP} --test"
        echo "========================================================"
    fi
else
    echo "[4/4] No target IP provided. To deploy live to board, run:"
    echo "      $0 <board-ip-address> [firmware-name.elf | --test]"
fi

echo "========================================================"
echo "Done!"
